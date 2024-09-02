#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <regex.h>
#include <string.h>
#include <math.h>
#https://github.com/gonzalorodriguezrovira


/*Buffer size*/
#define BUF_SIZE 1024
/*Delimiter between lines*/
#define DELIMITER "\n"

// Auxiliar functions
int countDigit(int n)
{
    if (n/10 == 0)
        return 1;
    return 1 + countDigit(n / 10);
}

void print_help(int exit_value)
{
    fprintf(stderr, "Uso: ./minigrep -r REGEX [-s BUFSIZE] [-v] [-c] [-h]\n\t-r REGEX Expresión regular.\n\t-s BUFSIZE Tamaño de los buffers de lectura y escritura en bytes (por defecto, 1024).\n\t-v Acepta las líneas que NO sean reconocidas por la expresión regular (por defecto, falso).\n\t-c Muestra el número total de líneas aceptadas (por defecto, falso).\n\n");
    exit_value == 0 ? exit(EXIT_FAILURE) : exit(EXIT_SUCCESS);
}

int find_last(const char* str) {
    int last_position = -1;
    int position = 0;

    while (str[position] != '\0') {
        if (str[position] == '\n')
            last_position = position;
        position++;
    }
    return last_position;
}

int write_all(int fd,char *buf,ssize_t buf_size){
    ssize_t num_written = 0;
    ssize_t num_left = buf_size;
    char *buf_left=buf;

    while((num_left>0)&&(num_written=write(fd,buf_left,num_left))!=-1){
        buf_left+=num_written;
        num_left-=num_written;
    }
    if (num_written == -1){
        fprintf(stderr,"ERROR: write()\n");
        exit(EXIT_FAILURE);
    }
    write(fd, "\n", 1);
    return buf_size;
}

int read_all(int fd, char *buf, size_t size){
    ssize_t numRead = 0;
    ssize_t numLeft = size;
    char *bufLeft = buf;
    int senal = 1;
    while (senal && numLeft > 0 && (numRead = read(fd, bufLeft, numLeft)) != -1)
    {
        if (numRead == 0 && numLeft > 0)
            senal = 0;
        bufLeft += numRead;
        numLeft -= numRead;
    }
    if (numRead == -1){
        fprintf(stderr,"ERROR: read()\n");
        exit(EXIT_FAILURE);
    }
    return size - numLeft;
}

/*Splits the string into substrings depending on the delimiter used.
Returns an array of strings and gives the number of substrings created*/
char **splitString(const char *string, int *numSubstrings) {
    // counter of numbers of substrings
    (*numSubstrings) = 0;
    for (int i = 0; string[i] != '\0'; i++ ){
        if (string[i] == '\n') (*numSubstrings)++;
        if (string[i+1] == '\0' && string[i] != '\n')(*numSubstrings)++;
    }
    // Asign memory to the substrings array
    char **substrings = (char**)malloc((*numSubstrings) * sizeof(char*));
    if (substrings == NULL) {
        fprintf(stderr,"ERROR: malloc()\n");
        exit(EXIT_FAILURE);
    }

    // Initialize variables
    for (size_t i = 0; i < (*numSubstrings); i++)
        substrings[i] = NULL;

    int i = 0;
    int start = 0;
    int idx = 0;

    // Iterate through the string
    while (string[i] != '\0') {
        // Check the delimiter
        if (string[i] != '\n') {
            i++;
            continue;
        }
        // Extract the substring and store it in the array
        int length = i - start;
        char *substring = (char *)malloc((length + 1) * sizeof(char));

        for (int j = 0; j < length; j++)
            substring[j] = string[start + j];

        substring[length] = '\0';

        substrings[idx] = substring;
        idx++;

        // Move the start index to the next character
        start = i + 1;
        // Move to the next character in the string
        i++;
    }

    // 'i' can only be greater or equal. If it is greater, there is still another substring to store.
    if (i == start) return substrings;

    int length = i - start;
    char *substring = (char *)malloc((length + 1) * sizeof(char));
    for (int j = 0; j < length; j++)
        substring[j] = string[start + j];
    substring[length] = '\0';
    substrings[idx] = substring;

    return substrings;
}

/*Given an string, that may contains substrings, it writes in the output the strings that the user is looking for*/
void matches(int output_file, char *buffer_read, int *numSubstrings, regex_t regex, int nmatch, regmatch_t pmatch, int inv,int iscont,int *cont){
    char **substrings = splitString(buffer_read, numSubstrings);
    for (unsigned int i = 0; i < *numSubstrings; i++) {
        if (strlen(substrings[i]) >= 4096){
            fprintf(stderr, "ERROR: Línea demasiado larga\n");
            exit(EXIT_FAILURE);
        }
        int mactch = regexec(&regex, substrings[i], nmatch, &pmatch, 0);
        if (((mactch == REG_NOMATCH) && inv) || ((mactch != REG_NOMATCH) && !inv)){
            if(!iscont)
                write_all(output_file, substrings[i], strlen(substrings[i]));
            (*cont)++;
        }
    }
    for (int i = 0; i < *numSubstrings; i++)
        free(substrings[i]);
    free(substrings);
}

/*Gets from a buffer the processable string if possible.
If there is a non yet procesabble part of the string, it is stored for the next buffer, in the next call*/
void process_input(ssize_t* index, int num_read, char* buffer_read, char* leftover, int numSubstrings, regex_t regex, int *nmatch, regmatch_t pmatch, int inv, int iscont, int *cont){
    int pos = find_last(buffer_read);       // Position of the last '\n' in the string

    // If it does not have a '\n'.
    if (pos == -1){
        for (ssize_t counter = 0; counter < num_read; counter++)
            (leftover[(*index) + (counter)] = buffer_read[counter]);

        (*index) += num_read;
        return;
    }
    // Increase the position so it points to the next character after \n (and so that we can store the rest of the string to process it too)
    pos++;

    // We have found at least one \n, then we process until the last \n. We keep the rest of the string
    char *toProcess;
    if ((toProcess = (char*)malloc(sizeof(char) * ((*index) + pos + 1))) == NULL){ // +1 so that the string can end in \0
        fprintf(stderr,"ERROR: malloc()\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < (*index) + pos; i++)
        toProcess[i] = '\0';

    for (int i = 0; i < (*index); i++)
        toProcess[i] = leftover[i];

    for (int i = 0; i < pos; i++)
        toProcess[(*index)+i] = buffer_read[i];

    toProcess[(*index) + pos] = '\0';

    // Process the string

    matches(STDOUT_FILENO, toProcess, &numSubstrings, regex, *nmatch, pmatch, inv, iscont, cont);

    /*Save what is after the last \n*/
    for (int i = 0; leftover[i] != '\0'; i++)
        leftover[i] = '\0';

    for(int i = pos; i < num_read; i++)
        leftover[i-pos] = buffer_read[i];

    (*index) = num_read-pos;
    free(toProcess);
}

int main(int argc, char *argv[])
{
    //VARIABLES
    int opt;

    char *re = NULL;
    regex_t regex;
    regex_t delimiter;
    regmatch_t pmatch[1];
    int nmatch = 1;
    int inv = 0;
    int iscont = 0;
    int numSubstrings = 0;

    //Contador de lines
    int cont = 0;

    ssize_t num_read;
    int buff_size = BUF_SIZE;
    char *buffer_read;

    char leftover[4096] = "";

    for (int i = 0; i < 4096; i++)
        leftover[i] = '\0';

    // ARGUMENTS
    optind = 1;
    while ((opt = getopt(argc, argv, "r:s:vch")) != -1)
    {
        switch (opt)
        {
        case 'r':
            re=optarg;
            break;
        case 's':
            buff_size = atoi(optarg);
            break;
        case 'v':
            inv=1;
            break;
        case 'c':
            iscont=1;
            break;
        case 'h':
            print_help(1);
            break;
        default:
            print_help(0);
            break;
        }
    }

    // SOME ERRORS
    if (re == NULL){
        fprintf(stderr, "ERROR: REGEX vacía\n");
        exit(EXIT_FAILURE);
    }

    if(buff_size < 1 || buff_size > 1048576){
        fprintf(stderr, "ERROR: BUFSIZE debe ser mayor que 0 y menor que o igual a 1 MB\n");
        exit(EXIT_FAILURE);
    }

    // INIT REGEX
    int regex_return_code;

    if ((regex_return_code = regcomp(&regex, re, REG_NEWLINE|REG_EXTENDED)) != 0){
        switch (regex_return_code)
        {
        case REG_ECTYPE:
            fprintf(stderr, "ERROR: REGEX mal construida\n");
            break;
        default:
            break;
        }
        exit(EXIT_FAILURE);
    }

    if (regcomp(&delimiter, DELIMITER, REG_NEWLINE|REG_EXTENDED))
        exit(EXIT_FAILURE);

    // INIT BUFFER
    if ((buffer_read = (char *) malloc(buff_size * sizeof(char))) == NULL)
    {
        fprintf(stderr,"ERROR: malloc()\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < buff_size; i++){
        buffer_read[i] = '\0';
    }

    // READ TMP FILE
    int tmp;
    if ((tmp = open("listado", O_WRONLY| O_CREAT | O_TRUNC,S_IRWXU)) == -1)
    {
        perror("open()");
        exit(EXIT_FAILURE);
    }

    ssize_t index = 0;

    while ((num_read = read_all(STDIN_FILENO, buffer_read, buff_size)) > 0){
        buffer_read[num_read] = '\0';       // To delimit the buffer
        process_input(&index, num_read, buffer_read, leftover, numSubstrings, regex, &nmatch, pmatch[1], inv, iscont, &cont);
    }

    /*Treat last line*/
    matches(STDOUT_FILENO, leftover, &numSubstrings, regex, nmatch, pmatch[1], inv, iscont, &cont);

    if(iscont){
        int ndigit = countDigit(cont);
        char buffer[ndigit+1];                          // +1 because we will need the an extra character for '\0'
        snprintf(buffer, ndigit+1, "%d", cont);
        write_all(STDOUT_FILENO,buffer,ndigit);
    }


    free(buffer_read);

    return EXIT_SUCCESS;
}
