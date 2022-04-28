#include <stdio.h> 			
#include <string.h>			
#include <stdlib.h>			
#include <syslog.h>			// Możliwość użycia logu systemowego.
#include <dirent.h>			// Udostępnia funkcje i struktury umożliwiające listowanie plików.
#include <signal.h>			// Obsługa sygnałów.
#include <unistd.h>			// Standardowa biblioteka dla systemów Linux udostepniająca makra i funkcje 
#include <sys/types.h>		// Różne typy danych.
#include <fcntl.h>			// Działanie na plikach - otwieranie, zamykanie, ustawianie uprawnień.
#include <sys/mman.h>		// Deklaracje zarządzania pamięcią, używane do mapowania.
#include <sys/stat.h>		// Definiuje strukury danych zwracane przez funkcje stat(), fstat(), lstat().
#include <utime.h> 			// Modyfikacja czasu		//sprawdzic czy potrzebne
#include <limits.h> 		// Limity systemowe (w celu pobrania PATH_MAX, czyli maksymalna dlugosc sciezki w systemie)    //sprawdzic czy potrzebne

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


int fileOrDir(char* path){ 							//funkcja sprawdzająca czy ścieżka jest katalogiem, czy plikiem
    struct stat fileStatistic;
    stat(path,&fileStatistic);x`
    if(S_ISREG(fileStatistic.st_mode)) return 1;	//regular file
    if(S_ISDIR(fileStatistic.st_mode)) return 0;    //directory
    return -1;      //error
}

void signalHandle(int sig){ 												// Obsługa sygnału - funkcja wypisująca do logu zależnie od typu sygnału
    if(sig == SIGALRM) syslog(LOG_INFO, "Daemon woken up automatically");
    else if (sig == SIGUSR1) syslog(LOG_INFO, "Daemon woken up by SIGUSR1 signal");
}

void setSignal(struct sigaction newSignal, sigset_t newSet, int sig){  //
    sigemptyset(&newSet);

    newSignal.sa_handler = &signalHandle;
    newSignal.sa_flags = 0;						
    newSignal.sa_mask = newSet;

    sigaction(sig, &newSignal, NULL);
}

int checkParameters(int argc, char* argv[]){ 		// Funkcja sprawdzająca poprawność argumentów podanych do programu
    if (argc < 3 || argc > 8){
        syslog(LOG_ERR, "Wrong number of parameters");
        return -1;
    }
    else if (fileOrDir(argv[1]) != 0){
        syslog(LOG_ERR, "%s is not valid", argv[1]);
        return -1;
    }
    else if (fileOrDir(argv[2]) != 0){ 				// Sprawdzamy czy podana ścieżka docelowa jest katalogiem.
        syslog(LOG_ERR, "%s is not valid", argv[2]);
        return -1;
    }
    else{
        return 0;
    }
}

void setParameters(int argc, char* argv[]){			  // Funkcja sprawdzające parametry podane przez użytkownika do tablicy argv
    for (int i = 3; i < argc; i++){
        if (argv[i][0] == '-' && argv[i][1] == 't'){  //poszukiwanie flagi -t zmieniającej czas czuwania demona
            if (i+1 < argc && atoi(argv[i+1]) > 0){
                sleepTime = atoi(argv[i+1]);
            }
        }
        if (argv[i][0] == '-' && argv[i][1] == 'R'){  //poszukiwanie flagi -R włączającej rekurencyjne synchronizowanie
            recursion_option = 1;
        }
        if (argv[i][0] == '-' && argv[i][1] == 'd'){  //poszukiwanie flagi -d ustawiającej próg odróżnienia dużych plików
            if (i+1 < argc && atoi(argv[i+1]) > 0){
                cp_buffer = atoi(argv[i + 1]);
            }
        }
    }
}

char* pathLinking(char* path, char* fName){					//Funkcja, która tworzy ścieżki do rekurencyjnej synchronizacji
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

int checkAndSync(char* src,char* dst){		// Funkcja synchronizująca katalogi
    DIR* srcDir = opendir(src);
    DIR* dstDir = opendir(dst);

    struct dirent *currentFile;				// Biblioteka dirent.h udostępnia strukturę dirent, która określa aktualny element katalogu
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

    while ((currentFile = readdir(srcDir)) != NULL){		// Readdir zwraca wskaźniki na kolejne podkatalogi w danym strumieniu, jeśli nie ma więcej podkalogów, zwraca wartość NULL
        if((strcmp(currentFile->d_name, ".") != 0) && (strcmp(currentFile->d_name, "..") != 0) ){ 		// Kropka reprezentuje nazwę obecnego katalogu, dwie kropki katalogu wyżej
            srcFilePath = pathLinking(src, currentFile->d_name);
            dstFilePath = pathLinking(dst, currentFile->d_name);

            dstFStat.st_mtime = 0; //jeśli plik nie istniał to czas modyfikacji jest równy zero
            stat(srcFilePath,&srcFStat);
            file_exist = stat(dstFilePath,&dstFStat);   //jeśli plik istnieje to czas modyfikacji jest nadpisywany

            if(fileOrDir(srcFilePath) && srcFStat.st_mtime > dstFStat.st_mtime){  // Jeśli czas modyfikacji pliku źródłowego jest nowszy, należy kopiować
                if(srcFStat.st_size <= cp_buffer) readWriteCopy(srcFilePath,dstFilePath);
                else mapCopy(srcFilePath, dstFilePath, &srcFStat);
				chmod(dstFilePath, srcFStat.st_mode); 
            }else if(fileOrDir(srcFilePath) == 0 && recursion_option){ //  Jeżeli ścieżka jest katalogiem i użytkownik wybrał opcję rekurencyjnego kopiowania plików
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

int checkAndDelete(char* src,char* dst) { 		// Funkcja usuwająca pliki które nie znajdują się w ścieżce źródłowej
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
        if((strcmp(currentFile->d_name, ".") != 0) && (strcmp(currentFile->d_name, "..") != 0) ){ 
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
    struct sigaction autoSig, userSig;
    sigset_t autoSet, userSet;
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

        setSignal(autoSig, autoSet, SIGALRM);
        setSignal(userSig, userSet, SIGUSR1);
        syslog(LOG_INFO, "SynchronizeDemon fell asleep");
        alarm(sleepTime);
        pause();    //bez pauzy powtarza komunikaty w syslogu
        checkAndSync(argv[1], argv[2]); //sprawdzanie katalogu źródłowego w celu kopiowania
        checkAndDelete(argv[1], argv[2]); //sprawdzanie katalogu docelowego w celu usuwania

    }

    return 0;
}