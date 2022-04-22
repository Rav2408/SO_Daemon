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

int fileOrDir(char* path){
    struct stat fileStatistic;
    stat(path,&fileStatistic);
    if(S_ISREG(fileStatistic.st_mode)) return 1;    //regular file
    if(S_ISDIR(fileStatistic.st_mode)) return 0;    //directory
    return -1;      //error
}

char* pathLinking(char* path, char* fName){
    char* fullPath = malloc(MAX_PATH_LENGTH*sizeof(char));
    strcpy(fullPath,path);
    strcat(fullPath,"/");
    strcat(fullPath,fName);
    return fullPath;
}

int readWriteCopy(){
    return 0;
}

int mapCopy(){
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

    while (currentFile = readdir(srcDir) != NULL){
        if((strcmp(currentFile->d_name, ".") != 0) && (strcmp(currentFile->d_name, "..") != 0) ){ //istnieje lepsza wersja używając S_IFLNK
            srcFilePath = pathLinking(src, currentFile->d_name);
            dstFilePath = pathLinking(dst, currentFile->d_name);

            stat(srcFilePath,&srcFStat);
            file_exist = stat(dstFilePath,&dstFStat);

            if(fileOrDir(srcFilePath) && srcFStat.st_mtime > dstFStat.st_mtime){  //czas modyfikacji pokazuje że trzeba kopiować, wybierzmy rodzaj
                if(srcFStat.st_size <= cp_buffer) readWriteCopy();
                else mapCopy();
            }else if(fileOrDir(srcFilePath) == 0 && recursion_option){
                if(file_exist == -1){
                    mkdir(dstFilePath,srcFStat.st_mode);
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

    while (currentFile = readdir(srcDir) != NULL){
        if((strcmp(currentFile->d_name, ".") != 0) && (strcmp(currentFile->d_name, "..") != 0) ){ //istnieje lepsza wersja używając S_IFLNK
            srcFilePath = pathLinking(src, currentFile->d_name);
            dstFilePath = pathLinking(dst, currentFile->d_name);

            lstat(dstFilePath,&dstFStat);
            file_exist = fileOrDir(dstFilePath);

            if(file_exist == 1){    //jest zywkłym plikiem
                if(lstat(srcDir,&srcFStat) == -1){  //w katalogu źródłowym nie istnieje taki plik
                    unlink(dstFilePath);
                    syslog(LOG_INFO, "File has been deleted: %s", dstFilePath);
                }else if(file_exist == 0){      //jest katalogiem
                    unlink(dstFilePath);
                    syslog(LOG_INFO, "File has been deleted: %s", dstFilePath);
                }
            }else if(file_exist == 0 && recursion_option){      //jest katalogiem
                if(lstat(srcDir,&srcFStat) == -1){       //w źródłowym katalogu nie ma takiego katalogu
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
    

    // Open a log file
    openlog("SynchronizeDemon", LOG_PID, LOG_LOCAL0);
    syslog(LOG_INFO, "SynchronizeDemon has started");   //(int priority, const char* messege)
    while (1){
        syslog(LOG_INFO, "SynchronizeDemon woke up");
        printf("Concatenated String: %s\n", pathLinking("Hello","World!"));
        //sprawdzanie sygnałów
        //sprawdzanie katalogu źródłowego w celu kopiowania
        //sprawdzanie katalogu docelowego w celu usuwania
        syslog(LOG_INFO, "SynchronizeDemon fell asleep");
        sleep(10);
        //spanie

    }

    return 0;
}