#include <os.h>
#include "unzip.h"
#define NIO_REPLACE_STDIO
#include <nspireio.h>

#define DEBUG
const char PACSPIRE_ROOT[] = "/documents/pacspire";

typedef struct
{
	char name[21];
	char version[11];
	unsigned int timestamp;
} pkginfo;

#ifdef DEBUG
#	define debug(s, ...) printf(s, ##__VA_ARGS__)
#	define success(s, ...) \
		nio_color(nio_get_default(),NIO_COLOR_WHITE,NIO_COLOR_GREEN); \
		printf(s, ##__VA_ARGS__); \
		nio_color(nio_get_default(),NIO_COLOR_WHITE,NIO_COLOR_BLACK);
#	define warn(s, ...) \
		nio_color(nio_get_default(),NIO_COLOR_WHITE,NIO_COLOR_YELLOW); \
		printf(s, ##__VA_ARGS__); \
		nio_color(nio_get_default(),NIO_COLOR_WHITE,NIO_COLOR_BLACK);
#	define fail(s, ...) \
		nio_color(nio_get_default(),NIO_COLOR_WHITE,NIO_COLOR_RED); \
		printf(s, ##__VA_ARGS__); \
		nio_color(nio_get_default(),NIO_COLOR_WHITE,NIO_COLOR_BLACK);
#else
#	define debug(s, ...)
#	define success(s, ...)
#	define warn(s, ...)
#	define fail(s, ...)
#endif

pkginfo* parsePackageInfo(char* buffer)
{
	char* line;
	pkginfo* p = malloc(sizeof(pkginfo));
	p->name[0] = '\0';
	p->version[0] = '\0';
	p->timestamp = 0;
	
	line = strtok(buffer,"\r\n");
	while(line != NULL)
	{
		int len = strlen(line);
		
		int delimiter_pos = strcspn(line,"=");
		if(delimiter_pos == len)
		{
			free(p);
			return NULL;
		}
		
		line[delimiter_pos] = '\0';
		if(strcmp(line, "name") == 0)
		{
			strncpy(p->name,&line[delimiter_pos+1],20);
			p->name[20] = '\0';
		}
		else if(strcmp(line, "version") == 0)
		{
			strncpy(p->version,&line[delimiter_pos+1],10);
			p->name[10] = '\0';
		}
		else if(strcmp(line, "timestamp") == 0)
		{
			p->timestamp = (unsigned int)strtoul(&line[delimiter_pos+1],NULL,0);
			if(p->timestamp == 0)
			{
				free(p);
				return NULL;
			}
		}
		else
		{
			free(p);
			return NULL;
		}
		
		line = strtok(NULL,"\r\n");
	}
	
	if(p->name[0] == '\0' || p->version[0] == '\0' || p->timestamp == 0)
	{
		free(p);
		return NULL;
	}
	
	return p;
}

void* unzGetCurrentFileContent(unzFile uf)
{
	unz_file_info file_info;
	unzGetCurrentFileInfo(uf,&file_info,NULL,0,NULL,0,NULL,0);
	
	if(unzOpenCurrentFile(uf) != UNZ_OK)
		return NULL;
	
	char* buffer = malloc(file_info.uncompressed_size+1);
	if(buffer == NULL)
	{
		unzCloseCurrentFile(uf);
		return NULL;
	}
	
	if(unzReadCurrentFile(uf,buffer,file_info.uncompressed_size) < 0)
	{
		unzCloseCurrentFile(uf);
		return NULL;
	}
	buffer[file_info.uncompressed_size] = 0;
	unzCloseCurrentFile(uf); // Detect CRC errors?
	
	return buffer;
}

void* unzGetFileContent(unzFile uf, const char* filename)
{
	if(unzLocateFile(uf,filename,1) != UNZ_OK)
		return NULL;
	
	return unzGetCurrentFileContent(uf);
}

void* getFileContent(const char* filename)
{
	struct stat s;
	if(stat(filename,&s) == -1)
		return NULL;
	
	FILE* f = fopen(filename,"rb");
	if(f == NULL)
		return NULL;
	
	void* buffer = malloc(s.st_size);
	if(buffer == NULL)
	{
		fclose(f);
		return NULL;
	}
	
	if(fread(buffer,1,s.st_size,f) == 0)
	{
		fclose(f);
		free(buffer);
		return NULL;
	}
		
	fclose(f);
	return buffer;
}

int writeFileContent(const char* filename, void* buffer, size_t count)
{
	FILE* f = fopen(filename,"wb");
	if(f == NULL)
		return -1;
		
	if(fwrite(buffer,1,count,f) != count)
	{
		fclose(f);
		return -1;
	}
	
	fclose(f);
	return 0;
}

int removeDir(char* directory)
{
	DIR* dir;
	struct dirent* entry;
	
	dir = opendir(directory);
	if(dir == NULL)
		return -1;
		
	while((entry = readdir(dir)) != 0)
	{
		if(strcmp(entry->d_name,".") == 0 || strcmp(entry->d_name,"..") == 0)
			continue;
		
		char full_path[100];
		strcpy(full_path,directory);
		strcat(full_path,"/");
		strcat(full_path,entry->d_name);
		
		struct stat s;
		if(stat(full_path,&s) == -1)
		{
			closedir(dir);
			return -1;
		}
		
		if(s.st_mode & S_IFDIR)
		{
			if(removeDir(full_path) == -1)
			{
				closedir(dir);
				return -1;
			}
		}
		else
		{
			if(unlink(full_path) == -1)
			{
				closedir(dir);
				return -1;
			}
		}
	}
	
	closedir(dir);
	if(rmdir(directory) == -1)
		return -1;
	
	return 0;
}

int installPackage(char* file)
{
	unzFile uf = NULL;
	
	debug("opening package...");
	uf = unzOpen(file);
	if(uf == NULL)
	{
		fail(" failed\n");
		return -1;
	}
	success(" done\n");
	
	debug("unzipping package info...");
	void* buffer = unzGetFileContent(uf,"pkginfo.txt");
	if(buffer == NULL)
	{
		fail(" failed\n");
		unzClose(uf);
		free(buffer);
		return -1;
	}
	success(" done\n");
	
	debug("parsing package info...");
	pkginfo* p = parsePackageInfo(buffer);
	free(buffer);
	if(p == NULL)
	{
		fail(" failed\n");
		unzClose(uf);
		return -1;
	}
	success(" done\n");
	
	debug("Package %s\n",p->name);
	debug("Version: %s\n",p->version);
	debug("Timestamp: %d\n",p->timestamp);
	
	debug("checking if package is already installed...");
	char full_path[50];
	strcpy(full_path,PACSPIRE_ROOT);
	strcat(full_path,"/");
	strcat(full_path,p->name);
	struct stat s;
	if(stat(full_path,&s) == 0)
	{
		warn(" yes\n");
		
		debug("opening installed pkginfo.txt...");
		char pkginfo_path[60];
		strcpy(pkginfo_path,full_path);
		strcat(pkginfo_path,"/pkginfo.txt");
		buffer = getFileContent(pkginfo_path);
		if(buffer == NULL)
		{
			fail(" failed\n");
			free(p);
			unzClose(uf);
			return -1;
		}
		success(" done\n");
		
		debug("%s\n",buffer);
		
		debug("parsing installed pkginfo.txt...");
		pkginfo* p2 = parsePackageInfo(buffer);
		free(buffer);
		if(p2 == NULL)
		{
			fail(" failed\n");
			free(p);
			unzClose(uf);
			return -1;
		}
		success(" done\n");
		
		debug("checking if the package is newer than the installed version...");
		if(p->timestamp > p2->timestamp)
		{
			success(" yes\n");
			char message[50];
			sprintf(message,"Do you want to update %s (%s -> %s)?",p->name,p2->version,p->version);
			if(show_msgbox_2b("pacspire",message,"Update","Cancel") == 2)
			{
				fail("Installation aborted by user\n");
				free(p);
				free(p2);
				unzClose(uf);
				return -1;
			}
		}
		else
		{
			warn(" no\n");
			char message[50];
			sprintf(message,"The installed version of %s (%s) is newer than the one you are trying to install (%s). Continue?",p->name,p2->version,p->version);
			if(show_msgbox_2b("pacspire",message,"Continue","Cancel") == 2)
			{
				fail("Installation aborted by user\n");
				free(p);
				free(p2);
				unzClose(uf);
				return -1;
			}
		}
		
		free(p2);
		
		debug("Removing previous installation...");
		if(removeDir(full_path) == -1)
		{
			debug(" failed\n");
			unzClose(uf);
			return -1;
		}
		success(" done\n");
	}
	else
	{
		success(" no\n");
		char message[50];
		sprintf(message,"Do you want to install %s?",p->name);
		if(show_msgbox_2b("pacspire",message,"Install","Cancel") == 2)
		{
			fail("Installation aborted by user\n");
			unzClose(uf);
			return -1;
		}
	}
	
	debug("Creating directory %s...",full_path);
	if(mkdir(full_path, 0755) == -1)
	{
		if(errno == 17) // == EEXIST
		{
			warn(" exists\n");
		}
		else
		{
			fail(" failed\n");
			unzClose(uf);
			return -1;
		}
	}
	success(" done\n");
	
	debug("Going to first file in zip...");
	if(unzGoToFirstFile(uf) != UNZ_OK)
	{
		fail(" failed\n");
		unzClose(uf);
		return -1;
	}
	success(" done\n");
	
	do
	{
		char filename[50];
		unz_file_info file_info;
		unzGetCurrentFileInfo(uf,&file_info,filename,50,NULL,0,NULL,0);
		
		debug("Inflating file %s...",filename);
		char* buffer = unzGetCurrentFileContent(uf);
		if(buffer == NULL)
		{
			fail(" failed\n");
			unzClose(uf);
			return -1;
		}
		success(" done\n");
		
		debug("Writing content to file...");
		char file_path[60];
		strcpy(file_path,full_path);
		strcat(file_path,"/");
		strcat(file_path,filename);
		if(writeFileContent(file_path,buffer,file_info.uncompressed_size) == -1)
		{
			fail(" failed\n");
			free(buffer);
			unzClose(uf);
			return -1;
		}
		success(" done\n");
		
		free(buffer);
	}
	while(unzGoToNextFile(uf) == UNZ_OK);
	
	unzClose(uf);
	return 0;
}

int main(int argc, char** argv)
{
	#ifdef DEBUG
	clrscr();
	nio_use_stdio();
	#endif
	
	debug("pacspire (%s %s)\n",__DATE__,__TIME__);
	
	if(argc > 1)
	{
		debug("attempting to install package %s\n",argv[1]);
		installPackage(argv[1]);
	}
	else
	{
		debug("registering .pcs extension...");
		cfg_register_fileext("pcs","pacspire install");
		success(" done\n");
		show_msgbox("pacspire","pacspire has been installed. Click on a package to install it.");
	}
	
	#ifdef DEBUG
	nio_free_stdio();
	wait_key_pressed();
	#endif
	return 0;
}