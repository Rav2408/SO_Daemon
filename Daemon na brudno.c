int synchronize(char* source, char* destination) {
	
	DIR* sourceDir = opendir(source);
	DIR* destinationDir = opendir(destination);

	struct dirent* File_BUFF;
	struct stat sourceFStat, destinationFStat;

	char* sourcePath, * destinationPath;

	if (sourceDir == NULL)
	{
		syslog(LOG_ERR, "Error. Source directory couldn't be open: %s", src);
		return false;
	}
	
	if (destinationDir == NULL)
	{
		syslog(LOG_ERR, "Error. Destination directory couldn't be open: %s", src);
		return false;
	}

	while ((File_BUFF = readdir(sourceDir)) != NULL) {
		
		if(strcmp(File_BUFF)

	}

}