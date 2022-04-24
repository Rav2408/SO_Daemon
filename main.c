#include <stdio.h> 			// Standard Input/Output, czyli standardowe wejście-wyjście.
#include <string.h>			// Operacje na łańcuchach znaków.
#include <stdlib.h>			// Najbardziej podstawowe funkcje - exit(), malloc().
#include <syslog.h>			// Definicje dla rejestrowanie błędów systemu.
#include <dirent.h>			// Udostępnia funkcje, makra, i struktury, które umożliwiają łatwe trawersowanie katalogów.
#include <signal.h>			// Obsługa sygnałów.
#include <unistd.h>			// Znajduje się na prawie każdym systemie zgodnym ze standardem POSIX (Mac OS X, Linux, itd.) i udostępnia różne makra i funkcje niezbędne
// do tworzenia programów, które muszą korzystać z pewnych usług systemu operacyjnego.
#include <sys/types.h>		// Różne typy danych.
#include <fcntl.h>			// Działanie na plikach i inne operacje.
#include <sys/mman.h>		// Deklaracje zarządzania pamięcią.
#include <sys/stat.h>		// Nagłówek określa strukturę danych zwracanych przez funkcje fstat (), lstat (), stat ().
#include <utime.h> 			// modyfikacja czasu
#include <limits.h> 		// limity systemowe (w celu pobrania PATH_MAX, czyli maksymalna dlugosc sciezki w systemie)

#define MAX_PATH_LENGTH 2048

int cp_buffer = 2048;
int recursion_option = 0;
int sleepTime = 15;

//int checkIfPathIsDirectory(char* path)
//{
//    struct stat statistics;
//
//    stat(path, &statistics);
//
//    if (S_ISDIR(statistics.st_mode) != 0) return 1;
//    else return -1;
//}


int fileOrDir(char* path){
    struct stat fileStatistic;
    stat(path,&fileStatistic);
    if(S_ISREG(fileStatistic.st_mode)) return 1;    //regular file
    if(S_ISDIR(fileStatistic.st_mode)) return 0;    //directory
    return -1;      //error
}

int checkParameters(int argc, char* argv[])
{
    if (argc < 3 || argc > 8)
    {
        syslog(LOG_ERR, "Wrong number of parameters");
        return -1;
    }
    else if (fileOrDir(argv[1]) != 0)
    {
        syslog(LOG_ERR, "%s is not valid", argv[1]);
        return -1;
    }
    else if (fileOrDir(argv[2]) != 0)
    {
        syslog(LOG_ERR, "%s is not valid", argv[2]);
        return -1;
    }
    else
    {
        return 0;
    }
}

void setParameters(int argc, char* argv[])
{
    for (int i = 3; i < argc; i++)
    {
        if (argv[i][0] == '-' && argv[i][1] == 't')
        {
            if (i+1 < argc && atoi(argv[i+1]) > 0)
            {
                sleepTime = atoi(argv[i+1]);
            }
        }
        if (argv[i][0] == '-' && argv[i][1] == 'R')
        {
            recursion_option = 1;
        }
        if (argv[i][0] == '-' && argv[i][1] == 'd')
        {
            if (i+1 < argc && atoi(argv[i+1]) > 0)
            {
                cp_buffer = atoi(argv[i + 1]);
            }
        }
    }
}


char* pathLinking(char* path, char* fName){
    char* fullPath = malloc(MAX_PATH_LENGTH*sizeof(char));
    strcpy(fullPath,path);
    strcat(fullPath,"/");
    strcat(fullPath,fName);
    return (char*)fullPath;
}

int readWriteCopy(char* src, char* dst){

    FILE* srcFile = fopen(src, "rb");
    FILE* dstFile = fopen(dst, "wb");

    if(srcFile==NULL){
        syslog(LOG_ERR, "Error opening source file: %s", src);
        return 1;
    }

    if(dstFile==NULL){
        syslog(LOG_ERR, "Error opening file for writing: %s", dst);
        return 1;
    }

    char buffer[cp_buffer];
    size_t in,out;

    syslog(LOG_ERR, "Copying started...");
    while (1){
        in = fread(buffer, 1, cp_buffer, srcFile);
        if( in == 0 ) break;
        out = fwrite(buffer, 1, in, dstFile);
        if( out == 0 ) break;
    }
    syslog(LOG_ERR, "Copying is complete: %s", dst);
    fclose(srcFile);
    fclose(dstFile);

    return 0;
}

int mapCopy(char* src, char* dst, struct stat* srcStat){
    int srcFile = open(src, O_RDONLY);
    int dstFile = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0777);

    if(srcFile < 0){
        syslog(LOG_ERR, "Error opening source file: %s", src);
        return 1;
    }

    if(dstFile < 0){
        syslog(LOG_ERR, "Error opening file for writing: %s", dst);
        return 1;
    }

    char* buffer = mmap(0, srcStat->st_size, PROT_READ, MAP_SHARED, srcFile, 0);    //sprawdzić czy nie ma problemu z bufferem
    syslog(LOG_ERR, "Copying started...");
    if(write(dstFile, buffer, srcStat->st_size) < 0)
    {
        syslog(LOG_ERR, "Error on writing the file");
        return 1;
    }
    syslog(LOG_ERR, "Copying is complete: %s", dst);
    return 0;
}

int checkAndSync(char* src,char* dst){
    DIR* srcDir = opendir(src);
    DIR* dstDir = opendir(dst);

    struct dirent *currentFile;
    struct stat srcFStat, dstFStat;
    int file_exist;

    char* srcFilePath;
    char* dstFilePath;


    if(srcDir==NULL){
        syslog(LOG_ERR, "Error opening source directory: %s", src);
        return 1;
    }
    if(dstDir==NULL){
        syslog(LOG_ERR, "Error opening destination directory: %s", dst);
        return 1;
    }

    while ((currentFile = readdir(srcDir)) != NULL){
        if((strcmp(currentFile->d_name, ".") != 0) && (strcmp(currentFile->d_name, "..") != 0) ){ //istnieje lepsza wersja używając S_IFLNK
            srcFilePath = pathLinking(src, currentFile->d_name);
            dstFilePath = pathLinking(dst, currentFile->d_name);

            dstFStat.st_mtime = 0; //jeśli plik nie istniał to czas modyfikacji jest równy zero
            stat(srcFilePath,&srcFStat);
            file_exist = stat(dstFilePath,&dstFStat);   //jeśli plik istnieje to czas modyfikacji jest nadpisywany

            if(fileOrDir(srcFilePath) && srcFStat.st_mtime > dstFStat.st_mtime){  //czas modyfikacji pokazuje że trzeba kopiować, wybierzmy rodzaj
                if(srcFStat.st_size <= cp_buffer) readWriteCopy(srcFilePath,dstFilePath);
                else mapCopy(srcFilePath, dstFilePath, &srcFStat);
            }else if(fileOrDir(srcFilePath) == 0 && recursion_option){
                if(file_exist == -1){
                    mkdir(dstFilePath,srcFStat.st_mode);     //srcFStat.st_mode
                    syslog(LOG_INFO, "New directory has been created: %s", dstFilePath);
                }
                checkAndSync(srcFilePath,dstFilePath);
            }
        }
    }

    free(currentFile);
    closedir(srcDir);
    closedir(dstDir);
    return 0;
}

int checkAndDelete(char* src,char* dst) {
    DIR *srcDir = opendir(src);
    DIR *dstDir = opendir(dst);

    struct dirent *currentFile;
    struct stat srcFStat, dstFStat;
    int file_exist;

    char *srcFilePath;
    char *dstFilePath;

    if(dstDir==NULL){
        syslog(LOG_ERR, "Error opening destination directory: %s", dst);
        return 1;
    }

    while ((currentFile = readdir(dstDir)) != NULL){
        if((strcmp(currentFile->d_name, ".") != 0) && (strcmp(currentFile->d_name, "..") != 0) ){ //istnieje lepsza wersja używając S_IFLNK
            srcFilePath = pathLinking(src, currentFile->d_name);
            dstFilePath = pathLinking(dst, currentFile->d_name);

            lstat(dstFilePath,&dstFStat);
            file_exist = fileOrDir(dstFilePath);

            if(file_exist == 1){    //jest zywkłym plikiem
                if(lstat(srcFilePath,&srcFStat) == -1){  //w katalogu źródłowym nie istnieje taki plik
                    unlink(dstFilePath);
                    syslog(LOG_INFO, "File has been deleted1: %s", dstFilePath);
                }else if(file_exist == 0){      //jest katalogiem
                    unlink(dstFilePath);
                    syslog(LOG_INFO, "File has been deleted2: %s", dstFilePath);
                }
            }else if(file_exist == 0 && recursion_option){      //jest katalogiem
                if(lstat(srcFilePath,&srcFStat) == -1){       //w źródłowym katalogu nie ma takiego katalogu
                    checkAndDelete(srcFilePath,dstFilePath);
                    rmdir(dstFilePath);
                    syslog(LOG_INFO, "Directory has been deleted: %s", dstFilePath);
                }else{      //w źródłowym katalogu jest taki katalog
                    checkAndDelete(srcFilePath,dstFilePath);
                }
            }

        }
    }

    free(currentFile);
    closedir(srcDir);
    closedir(dstDir);
    return 0;
}

int main(int argc, char* argv[]){
    // Open a log file
    openlog("SynchronizeDemon", LOG_PID, LOG_LOCAL0);
    if(checkParameters(argc,argv) == -1) exit(-1);
    setParameters(argc,argv);

    pid_t process_id = 0;
    pid_t sid = 0;

    // Create child process
    process_id = fork();

    // Indication of fork() failure
    if (process_id < 0){
        printf("fork failed!\n");
        exit(1);    // Return failure in exit status
    }

    // PARENT PROCESS. Need to kill it.
    if (process_id > 0){
        printf("process_id of child process %d \n", process_id);
        exit(0);    // return success in exit status
    }

    //unmask the file mode?
    umask(0);

    //set new session
    sid = setsid();
    if(sid < 0){
        exit(1);    // Return failure
    }


    syslog(LOG_INFO, "SynchronizeDemon has started");   //(int priority, const char* messege)
    while (1){
        syslog(LOG_INFO, "SynchronizeDemon woke up");
        //sprawdzanie sygnałów
        checkAndSync(argv[1], argv[2]); //sprawdzanie katalogu źródłowego w celu kopiowania
        checkAndDelete(argv[1], argv[2]); //sprawdzanie katalogu docelowego w celu usuwania
        syslog(LOG_INFO, "SynchronizeDemon fell asleep");
        sleep(sleepTime);
        //spanie

    }

    return 0;
}