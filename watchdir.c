#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include <sys/time.h>
#include <limits.h>
#https://github.com/gonzalorodriguezrovira

/*Perms for log file if created*/
#define FILE_PERM 0644
//Our msgs have a string informing what happened (we asume we will not need more than 100 chars)
//and a file name (the name of the file is an array of 256 chars)
#define MSG_SIZE 356
//In case we need to print 2 file names or the string way too long
#define MSG_EXTENDED_SIZE 612

#define TIME_MSG_SIZE 20
#define MIN_SECONDS_TIMER 1
#define MAX_SECONDS_TIMER 60
#define VALID_NUMBER_DIRS 1

// These variables need to be global as they are used by signal handlers
int log_fd;
char *dir =".";
/*Log file path*/
char *logName ="/tmp/watchdir.log";

/*Previous scanned entries from directory*/
struct dirent **previous_entries = NULL;
/*Previous scanned entries count from directory*/
int previous_entries_count = 0;
/*Stats from previous entries*/
struct stat **prev_stats = NULL;
/*Boolean to check if log file needs to reset*/
int isResetFile = 0;

// Auxiliar functions
void print_help(int exit_value)
{
    fprintf(stderr, "Usage: ./watchdir [-n SECONDS] [-l LOG] [DIR]\n\tSECONDS Refresh rate in [1..60] seconds [default: 1].\n\tLOG     Log file.\n\tDIR     Directory name [default: '.'].\n\n");
    exit(exit_value);
}

int write_all(int fd,char *buf,ssize_t buf_size){
    ssize_t num_written = 0;
    ssize_t num_left = buf_size;
    char *buf_left=buf;

    while((num_left > 0) && (num_written = write(fd,buf_left,num_left)) != -1){
        buf_left+=num_written;
        num_left-=num_written;
    }
    return num_written == -1? -1: buf_size;
}

void write_msg(char *msg)
{
    if (write_all(log_fd, msg, strlen(msg)) == -1)
    {
        perror("write()");
        exit(EXIT_FAILURE);
    }
}

void open_log(char *log_file) {
    log_fd = open(log_file, O_CREAT | O_WRONLY | O_TRUNC, FILE_PERM);
    if (log_fd == -1) {
        perror("open()");
        exit(EXIT_FAILURE);
    }
}

int compare(const char *s1, const char *s2){
    int position = 0;
    while (s1[position] != '\0' && s2[position] != '\0') {
        if (s1[position] != s2[position]) return 0;
        position++;
    }
    return s1[position] == '\0' && s2[position] == '\0';
}

char *my_strncpy(char *dest, const char *src, size_t n) {
    size_t i;

    // Copy characters from src to dest up to n characters
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    if (src[i] == '\0')
        dest[i] = '\0';
    // If the length of src is less than n, pad the remaining dest with '\0'
    for (; i < n; i++) {
        dest[i] = '\0';
    }

    return dest;
}

// Checks and logs the modifications in directory functions
void fileAdded(struct dirent *current_entry, int current_count, struct dirent *previous_entry, int previous_count, int *j){
    if((*j) >= previous_count || previous_entry->d_ino != current_entry->d_ino){
        char *msg = (char *)malloc(MSG_SIZE);
        snprintf(msg, MSG_SIZE, "Creation: %s\n", current_entry->d_name);
        write_msg(msg);
        if (msg != NULL) free(msg);
    }
    else (*j)++;
}

void fileRemoved(struct dirent *current_entry, int current_count, struct dirent *previous_entry, int previous_count, int *j){
    if((*j) >= current_count || previous_entry->d_ino != current_entry->d_ino){
        char *msg = (char *)malloc(MSG_SIZE);
        snprintf(msg, MSG_SIZE, "Deletion: %s\n", previous_entry->d_name);
        write_msg(msg);
        if (msg != NULL) free(msg);
    }
    else (*j)++;
}

int fileNameModified(struct dirent *current_entry, struct dirent *previous_entry){
    if (compare(previous_entry->d_name, current_entry->d_name)) return 0;
    char *msg = (char *)malloc(MSG_EXTENDED_SIZE);
    snprintf(msg, MSG_EXTENDED_SIZE, "UpdateName: %s -> %s\n", previous_entry->d_name, current_entry->d_name);
    write_msg(msg);
    if (msg != NULL) free(msg);
    return 1;
}

int fileSizeModified(struct stat *curr_stat, struct stat **prev_stat, struct dirent *previous_entry){
    if (((long)(*prev_stat)->st_size) == ((long)curr_stat->st_size)) return 0;
    char *msg = (char *)malloc(MSG_SIZE);
    snprintf(msg, MSG_SIZE, "UpdateSize: %s: %ld -> %ld\n", previous_entry->d_name, (long)(*prev_stat)->st_size, (long)curr_stat->st_size);
    write_msg(msg);
    if (msg != NULL) free(msg);
    if (*prev_stat != NULL)
        free(*prev_stat);
    *prev_stat = curr_stat;
    curr_stat = NULL;
    return 1;
}

int fileDateModified(struct stat *curr_stat, struct stat **prev_stat, struct dirent *previous_entry){
    if (difftime((*prev_stat)->st_mtim.tv_sec, curr_stat->st_mtim.tv_sec) == 0) return 0;
    char prev_time[TIME_MSG_SIZE], curr_time[TIME_MSG_SIZE];
    strftime(prev_time, TIME_MSG_SIZE, "%Y-%m-%d %H:%M:%S", localtime(&(*prev_stat)->st_mtime));
    strftime(curr_time, TIME_MSG_SIZE, "%Y-%m-%d %H:%M:%S", localtime(&curr_stat->st_mtime));

    char *msg = (char *)malloc(MSG_SIZE);
    snprintf(msg, MSG_SIZE, "UpdateMtim: %s: %s -> %s\n", previous_entry->d_name, prev_time, curr_time);
    write_msg(msg);
    if (msg != NULL) free(msg);
    if (*prev_stat != NULL)
        free(*prev_stat);
    *prev_stat = curr_stat;
    curr_stat = NULL;
    return 1;
}

void fileModified(struct dirent *current_entry, struct dirent *previous_entry, struct stat **prev_stat){
    if (fileNameModified(current_entry, previous_entry)) return;

    struct stat *curr_stat = malloc(sizeof(struct stat));
    char posteriorPath[PATH_MAX];
    snprintf(posteriorPath, PATH_MAX, "%s/%s", dir, current_entry->d_name);

    if (stat(posteriorPath, curr_stat) == -1) {
        switch (errno)
        {
        case ENOENT:
            break;
        default:
            if (curr_stat != NULL)
                free(curr_stat);
            curr_stat = NULL;
            fprintf(stderr, "ERROR: stat()\n");
            exit(EXIT_FAILURE);
            break;
        }
    }

    if(fileSizeModified(curr_stat, prev_stat, previous_entry)) return;
    if(fileDateModified(curr_stat, prev_stat, previous_entry)) return;
}

/*
Compares the entries from the last scandir with the current entries. Prints a log into the log file.
It only logs 1 type of action at a time, using the difference of entries between the last scan and the current scan:
- diff == 0: The function 'fileModified' will only log one action if it is necessary. It goes through both
arrays at the same time once. The order of complexity is O(n), being n the size of both arrays.
- diff != 0: To check what files were deleted, we go through the longest array. At the same time, we go through the shortest array,
but its index only increases when the elements from both arrays match, otherwise it does not. When the elements do not match it is
because the element in the longest array was removed.
A similar aproach is used when files are added.
As we can see, the loop will repeat n times, being n the size of the longest array. The order of complexity is O(n) when
adding or removing files.
*/
void compare_directories(struct dirent **previous_entries, int previous_count, struct dirent **current_entries, int current_count) {
    if (isResetFile){              // File removed by signal SIGUSR1
        previous_count = current_count;
    }

    int max = previous_count > current_count ? previous_count : current_count;        // n (size of the biggest array)
    int diff = previous_count - current_count;
    int j = 0;

    /*Add action to log*/
    if (diff < 0)
        for (int i = 0; i < max; i++)
            fileAdded(current_entries[i], current_count, previous_entries == NULL ? NULL : previous_entries[j], previous_count, &j);
    if (diff > 0)
        for (int i = 0; i < max; i++)
            fileRemoved(current_entries == NULL ? NULL : current_entries[j], current_count, previous_entries[i], previous_count, &j);
    if (diff == 0 && !isResetFile)          // If the file is cleared, there is no need to update the log
        for (int i = 0; i < max; i++)
            fileModified(current_entries[i], previous_entries[i], &prev_stats[i]);

    /*Update size and info from previous stats*/
    if(diff == 0) return;   // Already updated

    if (prev_stats != NULL) {
        for (int i = 0; i < previous_count; i++){
            if (prev_stats[i] != NULL) free(prev_stats[i]);
            prev_stats[i] = NULL;
        }
        free(prev_stats);
        prev_stats = NULL;
    }

    prev_stats = malloc(current_count*(sizeof(*prev_stats)));
    struct stat *curr_stat = NULL;
    for(int i = 0; i < current_count; i++){
        curr_stat = malloc(sizeof(struct stat));
        char posteriorPath[PATH_MAX];
        snprintf(posteriorPath, PATH_MAX, "%s/%s", dir, current_entries[i]->d_name);
        if (stat(posteriorPath, curr_stat) == -1){
            switch (errno)
            {
            case ENOENT:
                break;
            default:
                fprintf(stderr, "ERROR: stat()\n");
                exit(EXIT_FAILURE);
                break;
            }
        }
        prev_stats[i] = curr_stat;
    }
    isResetFile = 0;
}


/*Signals handler installer*/
void signal_handler_instaler(int signal, void (*signal_handler)(int)){
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction)); // TODO: ver si memset está permitido aquí
    sa.sa_handler = signal_handler;
    sa.sa_flags = SA_NOCLDSTOP;
    sigemptyset(&sa.sa_mask);
    if (sigaction(signal, &sa, NULL) == -1) {
        fprintf(stderr, "ERROR: sigaction()\n");
        exit(EXIT_FAILURE);
    }
}

/*Ignores directories . and ..*/
int filter_regularf(const struct dirent *entry) {
    return !(compare(entry->d_name,".") || compare(entry->d_name,".."));
}

/*Gets entries shorted by ino*/
int compare_d_ino(const struct dirent **a, const struct dirent **b) {
    return ((*a)->d_ino > (*b)->d_ino);
}

int my_scandir(const char *dir, struct dirent ***namelist, int (*filter)(const struct dirent *), int (*compar)(const struct dirent **, const struct dirent **)) {
    DIR *dp;
    struct dirent *entry;
    struct dirent **list = NULL;
    size_t count = 0;

    dp = opendir(dir);
    if (dp == NULL) {
        switch (errno)
        {
        case ENOTDIR:
            fprintf(stderr, "ERROR: '%s' is not a directory.\n", dir);
            print_help(EXIT_FAILURE);
        default:
            fprintf(stderr, "ERROR: opendir()\n");
            exit(EXIT_FAILURE);
        }
    }

    while ((entry = readdir(dp)) != NULL) {
        if (filter && !filter(entry)) {
            continue;
        }

        struct dirent *copy = (struct dirent *)malloc(sizeof(struct dirent));
        if (copy == NULL) {
            perror("malloc");
            closedir(dp);
            if (list != NULL){
                for (size_t i = 0; i < count; ++i) {
                    free(list[i]);
                }
                free(list);
                list = NULL;
            }
            exit(EXIT_FAILURE);
        }

        copy->d_ino = entry->d_ino;
        copy->d_off = entry->d_off;
        copy->d_reclen = entry->d_reclen;
        copy->d_type = entry->d_type;

        my_strncpy(copy->d_name, entry->d_name, sizeof(entry->d_name) - 1);
        copy->d_name[sizeof(entry->d_name) - 1] = '\0';

        struct dirent **temp = realloc(list, (count + 1) * sizeof(struct dirent *));
        if (temp == NULL) {
            perror("realloc");
            if (copy != NULL) free(copy);
            copy = NULL;
            closedir(dp);
            if (list != NULL){
                for (size_t i = 0; i < count; ++i) {
                    free(list[i]);
                }
                free(list);
            }
            exit(EXIT_FAILURE);
        }
        list = temp;
        list[count++] = copy;
    }

    if (closedir(dp) == -1) {
        perror("closedir");
        if (list != NULL){
            for (size_t i = 0; i < count; ++i) {
                free(list[i]);
            }
            free(list);
        }
        exit(EXIT_FAILURE);
    }

    if (compar) {
        qsort(list, count, sizeof(struct dirent *), (int (*)(const void *, const void *))compar);
    }

    *namelist = list;
    return count;
}

/*Gets and compares entries from dir*/
void sigalrm_handler(int signal){
    if(signal!=SIGALRM) return;

    struct dirent **entries=NULL;   // Current entries
    int entry_count = my_scandir(dir, &entries, filter_regularf, compare_d_ino);

    if (entry_count == -1){
        switch (errno)
        {
        case ENOTDIR:
            fprintf(stderr, "ERROR: '%s' is not a directory.\n", dir);
            print_help(EXIT_FAILURE);
            break;
        default:
            fprintf(stderr, "ERROR: scandir()\n");
            exit(EXIT_FAILURE);
            break;
        }
    }

    // Compares the current and previous entries from dir
    compare_directories(previous_entries, previous_entries_count, entries, entry_count);
    // Cleans previous entries from dir
    if (previous_entries != NULL){
        for (int i = 0; i < previous_entries_count; i++){
            if (previous_entries[i] != NULL) free(previous_entries[i]);
            previous_entries[i] = NULL;
        }
        free(previous_entries);
        previous_entries = NULL;
    }
    previous_entries_count = 0;

    // Copies current entries into the previous ones (deep clone)
    if(entry_count != 0){
        previous_entries = malloc(entry_count * sizeof(*previous_entries));
        for(int i = 0; i < entry_count; i++){
            previous_entries[i] = (struct dirent *)malloc(sizeof(struct dirent));

            previous_entries[i]->d_ino = entries[i]->d_ino;
            previous_entries[i]->d_off = entries[i]->d_off;
            previous_entries[i]->d_reclen = entries[i]->d_reclen;
            previous_entries[i]->d_type = entries[i]->d_type;

            my_strncpy(previous_entries[i]->d_name, entries[i]->d_name, sizeof(entries[i]->d_name) - 1);
            previous_entries[i]->d_name[sizeof(entries[i]->d_name) - 1] = '\0';
        }
        previous_entries_count = entry_count;
    }
    else{
        previous_entries = NULL;
        previous_entries_count = 0;
    }

    // Frees current entries
    if (entries != NULL){
        for (int i = 0; i <entry_count; i++){
            if (entries[i] != NULL) free(entries[i]);
            entries[i] = NULL;
        }
        free(entries);
    }
    entries = NULL;
}

/*Clears the log file*/
void sigusr1_handler(int signal) {
    isResetFile = 1;

    if (signal != SIGUSR1) return;

    // Closes file
    if (close(log_fd) != 0) {
        perror("close()");
        exit(EXIT_FAILURE);
    }

    // Frees previous_entries
    if (previous_entries != NULL) {
        for (int i = 0; i < previous_entries_count; i++){
            if (previous_entries[i] != NULL) free(previous_entries[i]);
            previous_entries[i] = NULL;
        }
        free(previous_entries);
        previous_entries = NULL;
    }

    //Frees prev_stats
    if (prev_stats != NULL){
        for (int i = 0; i < previous_entries_count; i++){
            if (prev_stats[i] != NULL) free(prev_stats[i]);
            prev_stats[i] = NULL;
        }
        free(prev_stats);
        prev_stats = NULL;
    }

    previous_entries_count = 0;

    // Opens a new log file
    open_log(logName);
}

/*Frees memory when killed*/
void sigint_handler(int signal){
    if(signal != SIGINT) return;

    // Frees previous_entries
    if (previous_entries != NULL) {
        for (int i = 0; i < previous_entries_count; i++){
            if (previous_entries[i] != NULL) free(previous_entries[i]);
            previous_entries[i] = NULL;
        }
        free(previous_entries);
        previous_entries = NULL;
    }

    //Frees prev_stats
    if (prev_stats != NULL){
        for (int i = 0; i < previous_entries_count; i++){
            if (prev_stats[i] != NULL) free(prev_stats[i]);
            prev_stats[i] = NULL;
        }
        free(prev_stats);
        prev_stats = NULL;
    }

    if (close(log_fd) != 0) {
        perror("fclose()");
        exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
}

void timer_conf(int seconds) {
    struct itimerval timer;
    timer.it_interval.tv_sec = seconds;
    timer.it_interval.tv_usec = 0;
    timer.it_value.tv_sec = seconds;
    timer.it_value.tv_usec = 0;

    if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
        fprintf(stderr, "ERROR: setitimer()\n");
        exit(EXIT_FAILURE);
    }
}

void initialize(int freq)
{
    signal_handler_instaler(SIGINT, sigint_handler);
    signal_handler_instaler(SIGUSR1,sigusr1_handler);
    signal_handler_instaler(SIGALRM,sigalrm_handler);

    open_log(logName);
    timer_conf(freq);
    sigalrm_handler(SIGALRM);     // First scandir
}

int main(int argc, char *argv[])
{
    /*Frequency of scandir (in seconds)*/
    int freq=1;
    int opt;
    optind = 1;

    while ((opt = getopt(argc, argv, "n:l:h")) != -1)
    {
        switch (opt)
        {
        case 'n':
            freq=atoi(optarg);
            break;
        case 'l':
            logName=optarg;
            break;
        case 'h':
            print_help(EXIT_SUCCESS);  // exit success
            break;
        default:
            print_help(EXIT_FAILURE);  // exit failure
            break;
        }
    }

    // Checks args are OK
    if (argc - optind > VALID_NUMBER_DIRS){     //Just 1 dir
        fprintf(stderr, "ERROR: './watchdir' does not support more than one directory.\n");
        print_help(EXIT_FAILURE);
    }

    if (optind != argc) dir=argv[optind];

    if (freq < MIN_SECONDS_TIMER || freq > MAX_SECONDS_TIMER){
        fprintf(stderr, "ERROR: SECONDS must be a value in [1..60].\n");
        print_help(EXIT_FAILURE);
    }

    // Initializes signal handlers
    initialize(freq);
    while(1){}

    exit(EXIT_SUCCESS);
}
