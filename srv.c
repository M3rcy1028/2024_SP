#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <grp.h>	//getgrgid
#include <pwd.h>	//getpwuid
#include <time.h>	//ctime
#include <fcntl.h>

#define FILETAP 7
#define BUFFSIZE 1024
#define MAX_BUF 128

//get/put
#define FLAGS  (O_RDWR | O_CREAT | O_TRUNC)
#define ASCII_MODE  (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#define BIN_MODE   (S_IXUSR | S_IXGRP | S_IXOTH)
int MODE_TYPE = 0;	//0 -> bin, 1 -> ascii
char clipath[256];
//logfile
FILE *fp;
char srvpath[256];
int ifsrvopend = 1;
//data connection file descrypter
int datafd;
int chnum = 0;
char username[50];
//child process head node
struct child* head = NULL;
struct sockaddr_in cliaddr;
//declare functions
void log_file(char *log, int type);
int log_auth(int connfd);
int user_match(char *user, char *passwd);
char* convert_str_to_addr(char *str, unsigned int *port);
int NLST_process(char *buff, char *result_buff);
int cmp(const void *a, const void *b);
char* Myltoa(long long int v);
//child proccess struct
struct child
{
	int pid;				//child pid
	int port;				//child port number
	char userID[50];		//user name
	time_t start;			//child inserted time
	struct child* cNext;	//next child node
};
void cinsert(int pid, int port);
void cdelete(int id);
void cprint();
void sh_chld(int signum);
void sh_alrm(int signum);
int client_info(struct sockaddr_in *cliaddr);
void PrintPid(char* cd, int v);
void sh_int(int sig);
///////////////////////////////////////////////////////////////////////
// main															  	 //
// ================================================================= //
// Input: argc, **argv												 //
// (Input parameter Description) the number of arguments(argc) and	 //
//						input string array							 //
// Output: 1, 0													  	 //
// (Out parameter Description) 1-> success, 0-> fail				 //
// Purpose: connect to client and process FTP command				 //
///////////////////////////////////////////////////////////////////////
void main(int argc, char **argv)
{
	char *log_buf = (char*)malloc(sizeof(char*) * 512);
	char *host_ip;
	char temp[50];
	unsigned int port_num;
	int listenfd, connfd, clilen;
	struct sockaddr_in servaddr;
	if (argc != 2)
	{
		printf("usage : %s <PORT>\n", argv[0]);
		exit(-1);
	}
	time_t srvtime;
	/* Applying signal handler(sh_alrm) for SIGLRM*/
	signal(SIGALRM, sh_alrm);
	/* Applying signal handler(sh_chld) for SICHLD*/
	signal(SIGCHLD, sh_chld);
	//make socket
	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		printf("550 Server: Can't open stream socket.\n");
		exit(-1);
	}
	//store server information
	bzero((char*)&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;					//IPv4
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);	//IP address
	servaddr.sin_port = htons(atoi(argv[1]));		//port number
	if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
	{
		printf("550 Server: Can't bind local address.\n");
		exit(-1);
	}
	listen(listenfd, 5);
	//save the absolute path of logfile
	if (srvpath[0] == '\0')
	{
		strcpy(srvpath, getcwd(NULL, 256));
		strcat(srvpath, "/");
		strcat(srvpath, "logfile");
	}
	//save server opened time
	srvtime = time(NULL);
	if (!fp)
	{
		/* write log file*/
		log_buf[0] = '\0';
		sprintf(log_buf, "%s", ctime(&srvtime));
		log_buf[(strlen(log_buf) - 1)] = '\0';
		sprintf(log_buf, "%s Server is started\n", log_buf);
		log_file(log_buf, 0);
		log_buf[0] = '\0';
		ifsrvopend = 0;
	}
	//control connection
	for(;;)
	{
		pid_t pid;
		char FTPcom[50];
		char *SendMSG = (char*)malloc(sizeof(char*)*512);
		int n;	
		FILE *fp_checkIP;	//passwd file pointer
		clilen = sizeof(cliaddr);
		connfd = accept(listenfd, (struct sockaddr*)&cliaddr, &clilen);
		if ((pid = fork()) < 0)	printf("550 fork error\n");
		else if (pid == 0)	//child
		{
			time_t starttime = time(NULL);
			signal(SIGINT, sh_int);
			/////////////////////Accept Control Connection////////////////////
			char rcvIP[MAX_BUF];
			//inet_pton(AF_INET, "192.168.1.10", &cliaddr.sin_addr);
			strcpy(rcvIP, inet_ntoa(cliaddr.sin_addr));		//store client's ip address
			// printf("** Client is trying to connect **\n");
			// printf(" - IP:   %s\n", rcvIP);
			// printf(" - Port: %d\n", cliaddr.sin_port);
			/* if IP is valid */
			fp_checkIP = fopen("access.txt", "r");	//open IP access file
			if (fp_checkIP == NULL)					//cannot open file
			{
				printf("550 Fail to open \"access.txt\" **\n");
				exit(1);
			}
			char readfp[MAX_BUF];	//receive ip from file
			int ipCheck = 0;		//if ip is checked
			while(fgets(readfp, MAX_BUF, fp_checkIP))	//read file
			{
				int lenfp = strlen(readfp);
				readfp[lenfp - 1] = '\0';
				//printf("valid IP = %s\n IP : %s\n\n", readfp, rcvIP);
				if (strstr(readfp, "*"))	//wildcard
				{
					char *ptr = strtok(readfp, "*");
					if (!strncmp(ptr, rcvIP, strlen(ptr)))	//ip matches
					{
						ipCheck = 1;
						break;
					}
				}
				else	//no wildcard
				{
					if (!strncmp(readfp, rcvIP, lenfp))	//ip matches
					{
						ipCheck = 1;
						break;
					}
				}
			}
			fclose(fp_checkIP);
			if (ipCheck == 0)		//ip denied
			{
				strcpy(SendMSG, "431 This client can't access, Close the session.\n");
				write(connfd, SendMSG, strlen(SendMSG));
				usleep(10000);
				printf("%s", SendMSG);
				log_file(SendMSG, 1);
				continue;
			}
			//printf("** Client is connected **\n");	//success
			if (log_auth(connfd) == 0)				//log-in fail
			{
				close(connfd);
				continue;
			}
			/////////////////////End of Accept Control Connection////////////////////
			for(;;)
			{
				/////////////////////Get FTP command and execution////////////////////
				temp[0] = '\0';
				n = read(connfd, temp, 1024);
				temp[n] = '\0';
				if (strstr(temp, "PORT"))
				{
					printf("%s\n", temp);
					//get ip and port num
					host_ip = convert_str_to_addr(temp, (unsigned int*)&port_num);
					//printf("converting to %s\n", host_ip);
					strcpy(SendMSG, "200 Port command performed successful.\n");
					write(connfd, SendMSG, strlen(SendMSG));
					usleep(10000);
					printf("%s", SendMSG);
					log_file(SendMSG, 1);
					/////////////////////make data connection////////////////////
					int datalen;
					struct sockaddr_in dataddr;
					if ((datafd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
					{
						strcpy(SendMSG, "550 Failed to access.\n");
						write(connfd, SendMSG, strlen(SendMSG));
						usleep(10000);
						printf("%s", SendMSG);
						log_file(SendMSG, 1);
						exit(-1);
					}
					//store client information
					bzero((char*)&dataddr, sizeof(dataddr));
					dataddr.sin_family = AF_INET;					//IPv4
					dataddr.sin_addr.s_addr = inet_addr(host_ip);	//IP address
					dataddr.sin_port = htons(port_num);				//port number
					//printf("Data Socket ip : %s, port : %d\n", host_ip, port_num);
					if(connect(datafd, (struct sockaddr*)&dataddr, sizeof(dataddr)) < 0)
					{
						strcpy(SendMSG, "550 Failed to access.\n");
						write(connfd, SendMSG, strlen(SendMSG));
						usleep(10000);
						printf("%s", SendMSG);
						log_file(SendMSG, 1);
						exit(-1);
					}
					/////////////////////End of make data connection////////////////////
				}
				else if (strstr(temp, "NLST") || strstr(temp, "LIST"))
				{
					//print FTP command
					strcpy(SendMSG, temp);
					char *ptr = strtok(SendMSG, "\n");
					printf("%s\n", ptr);
					snprintf(log_buf, 512, "%s %s\n", ptr, strtok(NULL, "\n"));
					log_file(log_buf, 1);
					log_buf[0] = '\0';
					//PrintPid(SendMSG, getpid());
					///
					strcpy(SendMSG, "150 Opening data connection for directory list.\n");
					write(connfd, SendMSG, strlen(SendMSG));
					usleep(10000);
					printf("%s", SendMSG);
					log_file(SendMSG, 1);
					///
					char* result_buf = (char*)malloc(sizeof(char) * BUFFSIZE);	//store results
					if (NLST_process(temp, result_buf) < 0)
					{
						strcpy(SendMSG, "550 Failed transmission.\n");
						write(connfd, SendMSG, strlen(SendMSG));
						usleep(10000);
						printf("%s", SendMSG);
						log_file(SendMSG, 1);
						write(datafd, "Finished", 8);
						close(datafd);
					}
					else
					{
						log_file("reply & message", 2);
						usleep(10000);
						write(datafd, "Finished", 8);
						close(datafd);
						strcpy(SendMSG, "226 Complete transmission.\n");
						write(connfd, SendMSG, strlen(SendMSG));
						usleep(10000);
						printf("%s", SendMSG);
						log_file(SendMSG, 1);
					}
					//free(result_buf);
				}
				else if (strstr(temp, "PWD"))
				{
					usleep(10000);
					printf("%s", temp);
					log_file(temp, 1);
					sprintf(SendMSG, "257 \"%s\" is current directory.", getcwd(NULL, 256));
					printf("%s\n", SendMSG);
					write(connfd, SendMSG, strlen(SendMSG));
					log_file(SendMSG, 2);
					usleep(1000);
					write(connfd, "finished!", 9);
					temp[0] = '\0';
				}
				else if (strstr(temp, "CDUP"))
				{
					usleep(1000);
					printf("%s\n", temp);
					log_file(temp, 2);
					if (chdir("..") == 0)
					{
						sprintf(SendMSG, "250 CDUP command performed successfully.");
						printf("%s\n", SendMSG);
						log_file(SendMSG, 2);
						write(connfd, SendMSG, strlen(SendMSG));
					}
					else
					{
						sprintf(SendMSG, "550 ..: Can't find such file or directory.");
						printf("%s\n", SendMSG);
						log_file(SendMSG, 2);
						write(connfd, SendMSG, strlen(SendMSG));
					}
					usleep(1000);
					write(connfd, "finished!", 9);
					temp[0] = '\0';
				}
				else if (strstr(temp, "CWD"))
				{
					usleep(1000);
					char *ptr = strtok(temp, "\n");
					strcpy(log_buf, ptr);
					strcat(log_buf, " ");
					ptr = strtok(NULL, "\n");
					strcpy(log_buf, ptr);
					printf("%s\n", temp);
					log_file(log_buf, 2);
					if (chdir(ptr) == 0)
					{
						SendMSG[0] = '\0';
						sprintf(SendMSG, "250 CWD command performed successfully.");
						printf("%s\n", SendMSG);
						log_file(SendMSG, 2);
						write(connfd, SendMSG, strlen(SendMSG));
					}
					else
					{
						sprintf(SendMSG, "550 %s : Can't find such file or directory.", ptr);
						printf("%s\n", SendMSG);
						log_file(SendMSG, 2);
						write(connfd, SendMSG, strlen(SendMSG));
					}
					usleep(1000);
					write(connfd, "finished!", 9);
					temp[0] = '\0';
				}
				else if (strstr(temp, "MKD"))
				{
					int cnt = 0;
					char* path = strtok(temp, " ");
					printf("%s\n", path);
					while(path = strtok(NULL," "))			//read argument
					{
						SendMSG[0] = '\0';
						cnt++;
						if (!access(path, F_OK))			//already exist
						{
							usleep(10000);	//wait
							sprintf(SendMSG, "550 %s : can't create directory.", path);
							printf("%s\n", SendMSG);
							log_file(SendMSG, 2);
							write(connfd, SendMSG, strlen(SendMSG));
							continue;
						}
						else	//make directory
						{
							if(mkdir(path, 0755))
							{	
								usleep(10000);	//wait
								sprintf(SendMSG, "550 %s : can't create directory.", path);
								printf("%s\n", SendMSG);
								log_file(SendMSG, 2);
								write(connfd, SendMSG, strlen(SendMSG));
							}
							else
							{	
								usleep(10000);	//wait
								usleep(10000);	//wait
								sprintf(SendMSG, "250 MKD command performed successfully.");
								printf("%s\n", SendMSG);
								log_file(SendMSG, 2);
								write(connfd, SendMSG, strlen(SendMSG));
							}
						}
					}
					usleep(1000);
					write(connfd, "finished!", 9);
					temp[0] = '\0';
				}
				else if (strstr(temp, "RMD"))
				{
					int cnt = 0;
					char* path = strtok(temp, " ");
					printf("%s\n", path);
					while(path = strtok(NULL," "))			//read argument
					{
						SendMSG[0] = '\0';
						cnt++;
						if (!access(path, F_OK))			//already exist
						{
							if(rmdir(path))
							{	
								usleep(10000);	//wait
								sprintf(SendMSG, "550 %s : can't remove directory.", path);
								printf("%s\n", SendMSG);
								log_file(SendMSG, 2);
								write(connfd, SendMSG, strlen(SendMSG));
							}
							else
							{	
								usleep(10000);	//wait
								usleep(10000);	//wait
								sprintf(SendMSG, "250 RMD command performed successfully.");
								printf("%s\n", SendMSG);
								log_file(SendMSG, 2);
								write(connfd, SendMSG, strlen(SendMSG));
							}
						}
						else	//make directory
						{
							usleep(10000);	//wait
							sprintf(SendMSG, "550 %s : can't remove directory.", path);
							printf("%s\n", SendMSG);
							log_file(SendMSG, 2);
							write(connfd, SendMSG, strlen(SendMSG));
							continue;
						}
					}
					usleep(1000);
					write(connfd, "finished!", 9);
					temp[0] = '\0';
				}
				else if (strstr(temp, "DELE"))
				{
					int cnt = 0;
					char* path = strtok(temp, " ");
					printf("%s\n", path);
					while(path = strtok(NULL," "))			//read argument
					{
						SendMSG[0] = '\0';
						cnt++;
						if (!access(path, F_OK))			//already exist
						{
							if (unlink(path))
							{
								usleep(10000);	//wait
								sprintf(SendMSG, "550 %s : Can't find such file or directory.", path);
								printf("%s\n", SendMSG);
								log_file(SendMSG, 2);
								write(connfd, SendMSG, strlen(SendMSG));
							}
							else
							{
								usleep(10000);	//wait
								sprintf(SendMSG, "250 DELE command performed successfully.");
								printf("%s\n", SendMSG);
								log_file(SendMSG, 2);
								write(connfd, SendMSG, strlen(SendMSG));
							}
						}
						else	//make directory
						{
							usleep(10000);	//wait
							sprintf(SendMSG, "550 %s : Can't find such file or directory.", path);
							printf("%s\n", SendMSG);
							log_file(SendMSG, 2);
							write(connfd, SendMSG, strlen(SendMSG));
						}
					}
					usleep(1000);
					write(connfd, "finished!", 9);
					temp[0] = '\0';
				}
				else if (strstr(temp, "RNFR"))
				{
					char *from;
					char *to;
					//////////////////////// print command name //////////////////////
					from = strtok(temp, " ");		//RNFR
					printf("%s ", from);
					from = strtok(NULL, " ");		//old name
					printf("%s\n", from);
					to = strtok(NULL, " ");			//&
					to = strtok(NULL, " ");			//RNTO
					printf("%s ", to);
					to = strtok(NULL, " ");			//new name
					printf("%s\n", to);
					//////////////////////// End of print command name //////////////////////
					if (access(from, F_OK)) 	//not exist
					{
						sprintf(SendMSG, "550 %s: Can't find such file or directory.", from);
						printf("%s\n", SendMSG);
						log_file(SendMSG, 2);
						write(connfd, SendMSG, strlen(SendMSG));
					}
					else
					{
						//////////////////////// check same name /////////////////////
						sprintf(SendMSG, "350 File exists, ready to rename.");
						printf("%s\n", SendMSG);
						log_file(SendMSG, 2);
						write(connfd, SendMSG, strlen(SendMSG));
						usleep(10000);
						if (!access(to, F_OK)) 	//already exist
						{
							sprintf(SendMSG, "550 %s: Can't be renamed.", to);
							printf("%s\n", SendMSG);
							log_file(SendMSG, 2);
							write(connfd, SendMSG, strlen(SendMSG));
						//////////////////////// End of check same name //////////////////////
						}
						else 		
						{
							sprintf(SendMSG, "250 RNTO command succeeds");
							printf("%s\n", SendMSG);
							log_file(SendMSG, 2);
							write(connfd, SendMSG, strlen(SendMSG));
							usleep(10000);
							rename(from, to);
						}
					}
					//////////////////////// End of command processing //////////////////////
					usleep(1000);
					write(connfd, "finished!", 9);
					temp[0] = '\0';
				}
				else if (strstr(temp, "QUIT"))
				{
					//PrintPid(temp, getpid());
					printf("%s\n", temp);
					log_file(temp, 2);
					printf("221 Goodbye.\n");
					log_file("221 Goodbye.\n",1);
					close(datafd);
					close(connfd);
					log_file("LOG_OUT", starttime);
					exit(1);
				}
				else if (strstr(temp, "TYPE I"))	//bin
				{
					printf("%s", temp);
					log_file(temp, 1);
					MODE_TYPE = 0;
					//send message
					if (MODE_TYPE == 0)
					{
						sprintf(SendMSG, "201 Type set to I.");
						printf("%s\n", SendMSG);
					}
					else
					{
						sprintf(SendMSG, "502 Type doesn’t set.");
						printf("%s\n", SendMSG);
					}
					write(connfd, SendMSG, strlen(SendMSG));
					log_file(SendMSG, 2);
					usleep(1000);
					write(connfd, "finished!", 9);
					temp[0] = '\0';
				}
				else if (strstr(temp, "TYPE A"))	//ascii
				{
					printf("%s", temp);
					log_file(temp, 1);
					MODE_TYPE = 1;
					//send message
					//send message
					if (MODE_TYPE == 1)
					{
						sprintf(SendMSG, "201 Type set to A.");
						printf("%s\n", SendMSG);
					}
					else
					{
						sprintf(SendMSG, "502 Type doesn’t set.");
						printf("%s\n", SendMSG);
					}
					write(connfd, SendMSG, strlen(SendMSG));
					log_file(SendMSG, 2);
					usleep(10000);
					write(connfd, "finished!", 9);
					temp[0] = '\0';
				}
				else if (strstr(temp, "RETR"))	//get : srv - >cli
				{
					//print FTP command
					printf("%s\n", temp);
					log_file(temp, 2);
					//get filename
					char *filename = strtok(temp, " ");
					filename = strtok(NULL, " ");
					printf("filename : %s\n", filename);
					FILE *fdget = 0;
					if (MODE_TYPE)	//ascii
					{
						fdget = fopen(filename, "r");
						strcpy(SendMSG, "Opening ascii mode data connection for ");
						strcat(SendMSG, filename);
						strcat(SendMSG, ".\n");
						write(connfd, SendMSG, strlen(SendMSG));
						usleep(10000);
						printf("%s", SendMSG);
						log_file(SendMSG, 1);
					}
					else	//binary
					{
						fdget = fopen(filename, "rb");
						strcpy(SendMSG, "Opening binary mode data connection for ");
						strcat(SendMSG, filename);
						strcat(SendMSG, ".\n");
						write(connfd, SendMSG, strlen(SendMSG));
						usleep(10000);
						printf("%s", SendMSG);
						log_file(SendMSG, 1);
					}
					if (!fdget)
					{
						strcpy(SendMSG, "550 Failed transmission.\n");
						write(connfd, SendMSG, strlen(SendMSG));
						usleep(10000);
						printf("%s", SendMSG);
						log_file(SendMSG, 1);
						write(datafd, "Finished", 8);
						close(datafd);
					}
					else
					{
						log_file("reply & message", 2);
						usleep(10000);
						write(datafd, "Finished", 8);
						close(datafd);
						strcpy(SendMSG, "226 Complete transmission.\n");
						write(connfd, SendMSG, strlen(SendMSG));
						usleep(10000);
						printf("%s", SendMSG);
						log_file(SendMSG, 1);
						usleep(10000);
						write(datafd, "Finished", 8);
						close(datafd);
					}
				}
				else if (strstr(temp, "STOR"))	//put : cli -> srv
				{
					//print FTP command
					printf("%s\n", temp);
					log_file(temp, 2);
					//get filename
					char *filename = strtok(temp, " ");
					filename = strtok(NULL, " ");
					printf("filename : %s\n", filename);
					FILE *fdget = 0;
					if (MODE_TYPE)	//ascii
					{
						fdget = fopen(filename, "r");
						strcpy(SendMSG, "Opening ascii mode data connection for ");
						strcat(SendMSG, filename);
						strcat(SendMSG, ".\n");
						write(connfd, SendMSG, strlen(SendMSG));
						usleep(10000);
						printf("%s", SendMSG);
						log_file(SendMSG, 1);
					}
					else	//binary
					{
						fdget = fopen(filename, "rb");
						strcpy(SendMSG, "Opening binary mode data connection for ");
						strcat(SendMSG, filename);
						strcat(SendMSG, ".\n");
						write(connfd, SendMSG, strlen(SendMSG));
						usleep(10000);
						printf("%s", SendMSG);
						log_file(SendMSG, 1);
					}
					if (!fdget)
					{
						strcpy(SendMSG, "550 Failed transmission.\n");
						write(connfd, SendMSG, strlen(SendMSG));
						usleep(10000);
						printf("%s", SendMSG);
						log_file(SendMSG, 1);
						write(datafd, "Finished", 8);
						close(datafd);
					}
					else
					{
						log_file("reply & message", 2);
						usleep(10000);
						write(datafd, "Finished", 8);
						close(datafd);
						strcpy(SendMSG, "226 Complete transmission.\n");
						write(connfd, SendMSG, strlen(SendMSG));
						usleep(10000);
						printf("%s", SendMSG);
						log_file(SendMSG, 1);
						usleep(10000);
						write(datafd, "Finished", 8);
						close(datafd);
					}
				}
				else
				{
					sprintf(SendMSG, "550 Wrong command.");
					printf("%s\n", SendMSG);
					write(connfd, SendMSG, strlen(SendMSG));
					log_file(SendMSG, 2);
					usleep(10000);
					write(connfd, "finished!", 9);
					temp[0] = '\0';
				}
				
			}
			close(connfd);
		}
		else 	//parent
		{
			signal(SIGINT, sh_int);
			//printf("Child Process ID : %d\n", pid);		//print child PID
			cinsert(pid, cliaddr.sin_port);			//insert new child
			//alarm(0);
			//alarm(1);
		}
	}
	close(listenfd);
	return;
}
///////////////////////////////////////////////////////////////////////
// log_file														  	 //
// ================================================================= //
// Input: log														 //
// (Input parameter Description) client file descripter				 //
// Output: 1, 0													  	 //
// (Out parameter Description) 1-> success, 0-> fail				 //
// Purpose: authenticate client with ID and password				 //
///////////////////////////////////////////////////////////////////////
void log_file(char *log, int type)
{
	fp = fopen(srvpath, "a");		//open passwd file
	if (!fp)					//cannot open file
	{
		printf("550 Fail to open \"logfile\" **\n");
		//fputs("550 Fail to open \"logfile\" **\n", fp); 
		return;
	}
	if (type)
	{
		char timebuf[128];
		char buf[1024];
		time_t t = time(NULL);
		strcpy(timebuf, ctime(&t));
		timebuf[(strlen(timebuf)) - 1] = '\0';
		if (type == 1)	//without newline
			snprintf(buf, 1500, "%s [%s:%d] %s %s", timebuf, inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port, username, log);
		else if (type == 2)	//with newline
			snprintf(buf, 1500, "%s [%s:%d] %s %s\n", timebuf, inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port, username, log);
		else //print total service time
		{
			sprintf(buf, "%s [%s:%d] %s LOG_OUT\n[total service time : %ld sec]\n", 
			timebuf, inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port, username, (t - type));
			log_file(buf, 0);
		}
		fputs(buf, fp);
	}
	else fputs(log, fp); 
	fclose(fp);
	return;		
}
///////////////////////////////////////////////////////////////////////
// log_auth														  	 //
// ================================================================= //
// Input: connfd													 //
// (Input parameter Description) client file descripter				 //
// Output: 1, 0													  	 //
// (Out parameter Description) 1-> success, 0-> fail				 //
// Purpose: authenticate client with ID and password				 //
///////////////////////////////////////////////////////////////////////
int log_auth(int connfd)
{
	char user[MAX_BUF], passwd[MAX_BUF];	//buffer for ID and password
	char srvtime[512];
	char buf[512];
	srvtime[0] = '\0';
	buf[0] = '\0';
	int n, count = 1;
	time_t t = time(NULL); 
	strcpy(srvtime, ctime(&t));
	char year[4];
	strcpy(year, (srvtime + 20));
	year[4] = '\0';
	srvtime[strlen(srvtime) - 6] = '\0';
	snprintf(buf, 1024, "Connected to sswlab.kw.ac.kr.\n");
	write(connfd, buf, MAX_BUF);			//send ip access is successful
	usleep(10000);
	snprintf(buf, 1024, "220 sswlab.kw.ac.kr FTP server (version myftp[1.0] %s KST %s) ready\n", srvtime, year);
	write(connfd, buf, MAX_BUF);			//send ip access is successful
	while(1)
	{
		//reset buffer
		buf[0] = '\0';
		user[0] = '\0';
		passwd[0] = '\0';
		//printf("** User is trying to log-in (%d/3) **\n", count);
		n = read(connfd, user, MAX_BUF);	//read ID
		user[n] = '\0';					//remove new line character
		printf("%s\n", user);
		strcpy(username, user + 5);
		//send passwd message
		strcpy(buf, "331 Password required for sswlab\n");
		printf("%s", buf);
		log_file(buf, 1);
		write(connfd, buf, MAX_BUF);
		buf[0] = '\0';
		//
		n = read(connfd, passwd, MAX_BUF);	//read password
		passwd[n] = '\0';
		//print password 
		char *ptr = strtok(passwd, " ");
		printf("%s ", ptr);
		ptr = strtok(NULL, " ");
		int pswdlen = strlen(ptr);
		for (int i = 0; i < pswdlen; i++)
			printf("*");
		printf("\n");
		//
		if ((n = user_match(user + 5, ptr)) == 1)
		{
			/* write log file */
			strcpy(srvtime, ctime(&t));
			srvtime[strlen(srvtime) - 1] = '\0';
			snprintf(buf, 1024, "%s [%s:%d] %s LOG_IN\n", srvtime, inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port, user + 5);
			log_file(buf, 0);
			buf[0] = '\0';
			/* auth OK */
			ptr = strtok(user, " ");
			ptr = strtok(NULL, " ");
			sprintf(buf, "230 User %s logged in\n", ptr);
			printf("%s", buf);
			log_file(buf, 1);
			write(connfd, buf, MAX_BUF);
			return 1;		//success
		}
		else if (n == 0)	//user not matches
		{
			if (count >= 3)
			{
				/* write log file */
				strcpy(srvtime, ctime(&t));
				srvtime[strlen(srvtime) - 1] = '\0';
				snprintf(buf, 1024, "%s [%s:%d] %s LOG_FAIL\n", srvtime, inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port, user + 5);
				log_file(buf, 0);
				buf[0] = '\0';
				/* 3 times fail */
				strcpy(buf, "530 Failed to log-in\n");
				printf("%s", buf);
				log_file(buf, 1);
				write(connfd, buf, MAX_BUF);
				buf[0] = '\0';
				return 0;	//fail
			}
			strcpy(buf, "430 Invalid username or password\n");
			printf("%s", buf);
			log_file(buf, 1);
			write(connfd, buf, MAX_BUF);
			buf[0] = '\0';
			count++;		//increase fail count
			continue;		//continue authentication
		}
	}
	return 0;				//fail
}
///////////////////////////////////////////////////////////////////////
// user_aut														  	 //
// ================================================================= //
// Input: *user, *passwd											 //
// (Input parameter Description) ID(user) and password(passwd)		 //
// Output: 1, 0													  	 //
// (Out parameter Description) 1-> success, 0-> fail				 //
// Purpose: check if ID and password matches						 //
///////////////////////////////////////////////////////////////////////
int user_match(char *user, char *passwd)
{
	FILE *fpwd;
	struct passwd *pw;
	fpwd = fopen("passwd", "r");		//open passwd file
	if (fpwd == NULL)					//cannot open file
	{
		printf("550 Fail to open \"passwd\" **\n");
		exit(1);
	}
	strcpy(username, user);
	while((pw = fgetpwent(fpwd)) != NULL) 	//get file content
	{
		if (!strcmp(pw->pw_name, user) && !strcmp(pw->pw_passwd, passwd))	//matches
		 {
		 	fclose(fpwd);
		 	return 1;	//success
		 }
	 }   
	fclose(fpwd);			//close file
	return 0;			//fail
}
///////////////////////////////////////////////////////////////////////
// convert_str_to_addr												//
// =================================================================//
// Input: char *str, unsigned int *port								//
// (Input parameter Description) PORT command and 					//
//								port array to store data port 		//
// Output: ip addr : success, NULL : error detected			    	//
// (Out parameter Description) return ip address	         		//
//							if PORT command is failed, return NULL	//
// Purpose: decompose PORT command and 								//
//							get ip address and port number			//
///////////////////////////////////////////////////////////////////////
char* convert_str_to_addr(char *str, unsigned int *port)
{
	char *addr = (char*)malloc(sizeof(50));		//ip address
	char *ptr = strtok(str, " ");				//remove PORT
	ptr = strtok(NULL, ",");
	strcpy(addr, ptr);
	for (int i = 0; i < 3; i++)					//get ip address
	{
		ptr = strtok(NULL, ",");
		sprintf(addr, "%s.%s", addr, ptr);
	}
	//get port number
	unsigned int upper = atoi(strtok(NULL, ","));
	unsigned int down = atoi(strtok(NULL, ","));
	*port = (unsigned)((upper << 8) | down);
	return addr;
}
///////////////////////////////////////////////////////////////////////
// Myltoa															//
// =================================================================//
// Input: v, base													//
// (Input parameter Description) long long int v, int base			//
// Output: character 												//
// (Out parameter Description) integer converted into character		//
// Purpose: convert long long int into character					//
///////////////////////////////////////////////////////////////////////
char* Myltoa(long long int v)
{
	if (v == 0) return "0";
	static char buf[128] = {0};
	int i = 30;
	for (; v && i; --i, v /= 10)
		buf[i] = "0123456789abcdef"[v % 10];
	return &buf[i+1];
}
///////////////////////////////////////////////////////////////////////
// cmp																//
// =================================================================//
// Input: a, b														//
// (Input parameter Description) char pinter a and b				//
// Output: result of comparing chracter a and b						//
// (Out parameter Description) a or b is bigger than its opposite	//
// Purpose: to compare character a and b							//
///////////////////////////////////////////////////////////////////////
int cmp(const void *a, const void *b) 
{
    return strcmp(*(const char **)a, *(const char **)b);
}
///////////////////////////////////////////////////////////////////////
// NLST_process														//
// =================================================================//
// Input: char *buff, char *result_buff								//
// (Input parameter Description) receive NLST command and			//
//								store result in result_buff			//
// Output: 1 : success, -1 : error detected							//
// (Out parameter Description) print information and return 1, 		//
//							if error is detected return -1			//
// Purpose: parsing NLST command and send result					//
///////////////////////////////////////////////////////////////////////
int NLST_process(char *buff, char *result_buff)
{
	char* com = (char*)malloc(sizeof(char) * BUFFSIZE);
	char* path = (char*)malloc(sizeof(char) * BUFFSIZE);
	char* inf = (char*)malloc(sizeof(char) * BUFFSIZE);
	int aflag = 0, lflag = 0;	//option
	//////////////////////// option setting //////////////////////
	if (strstr(buff, "LIST") != NULL)
	{
		aflag = 1;
		lflag = 1;
	}
	else if (strstr(buff, "-al") != NULL)
	{
		aflag = 1;
		lflag = 1;
		//printf("NLST -al\n");
		//write(STDOUT_FILENO, "NLST -al\n", strlen("NLST -al\n"));
	} //end of if
	else if (strstr(buff, "-a") != NULL) 
	{
		aflag = 1;
		//printf("NLST -a\n");
		//write(STDOUT_FILENO, "NLST -a\n", strlen("NLST -a\n"));
	}
	else if (strstr(buff, "-l") != NULL) 
	{
		lflag = 1;
		//printf("NLST -l\n");
		//write(STDOUT_FILENO, "NLST -l\n", strlen("NLST -l\n")); 
	}
	else 
	{
		//printf("NLST\n");
		//write(STDOUT_FILENO, "NLST\n", strlen("NLST\n"));
	}
	path = strtok(buff, "\n");	//command and option
	path = strtok(NULL, "\n");	//path
	if (path)
	{
		if (strstr(path, "./"))
		{
			com = getcwd(NULL, 256);
			strcat(com, "/");
			strcat(com, path + 2);
			strcpy(path, com);
		}
		else 
		{
			path[strlen(path)] = '\0';
		}
	}
	else
	{
		path = getcwd(NULL, 256);
	}	
	//////////////////////// End of option setting //////////////////////
	DIR *dp;				//directory pointer
	struct dirent *dirp;	//dirent struct pointer
	struct stat st;			//stat struct
	char *fileArr[1024];	//store file name
	int cnt = 0;			//the number of stored file name
	dp = opendir(path);		//open path directory
	stat(path, &st);		//store path status
	//////////////////////// check authority //////////////////////
	if (access(path, F_OK) < 0)	//Not exist file
	{
		sprintf(result_buff, "550 %s: Can't find such file or directory.\n", path);
		printf("%s", result_buff);
		log_file(result_buff, 1);
		write(datafd, result_buff, strlen(result_buff));	//send result to client
		return -1;
	} //end of if
	if (access(path, R_OK) < 0)	//can't access file
	{
		sprintf(result_buff, "550 %s: Can't find such file or directory.\n", path);
		printf("%s", result_buff);
		log_file(result_buff, 1);
		return -1;
	} //end of if
	//////////////////////// End of check authority //////////////////////
	if (!S_ISDIR(st.st_mode)) //file
	{
		//write(STDOUT_FILENO, "file\n", 5); 
		result_buff[0] = '\0';
		if (lflag)	//l option enabled
		{
			//print file format
			if (S_ISDIR(st.st_mode)) strcat(result_buff, "d");			//directory
			else if (S_ISFIFO(st.st_mode)) strcat(result_buff, "p");	//FIFO
			else if (S_ISLNK(st.st_mode)) strcat(result_buff, "l");	//link
			else if (S_ISSOCK(st.st_mode)) strcat(result_buff, "s");	//socket
			else if (S_ISBLK(st.st_mode)) strcat(result_buff, "b");	//block
			else if (S_ISCHR(st.st_mode)) strcat(result_buff, "c");	//character device
			else strcat(result_buff, "-");										//regular file
			//print authority
			for (int i = 0; i < 3; i++)
			{
				if (st.st_mode & (S_IREAD >> i*3)) strcat(result_buff, "r");	//read
				else strcat(result_buff, "-");								//non
				if (st.st_mode & (S_IWRITE >> i*3)) strcat(result_buff, "w");	//write
				else strcat(result_buff, "-");								//non
				if (st.st_mode & (S_IEXEC >> i*3)) strcat(result_buff, "x");	//execute
				else strcat(result_buff, "-");								//non
			} //end of for
			strcat(result_buff, " ");
			//print the number of link
			inf = Myltoa((long long)st.st_nlink);
			for (int i = 0; i < (3 - strlen(inf)); i++) strcat(result_buff, " "); //file delimeter
			strcat(result_buff, inf);
			strcat(result_buff, " ");
			//print file owner
			strcat(result_buff, getpwuid(st.st_uid)->pw_name);
			strcat(result_buff, " ");
			//print file group name
			strcat(result_buff, getgrgid(st.st_gid)->gr_name);
			strcat(result_buff, " ");
			//print file size
			inf = Myltoa((long long)st.st_size);
			//Right align
			for (int i = 0; i < (FILETAP - strlen(inf)); i++) strcat(result_buff, " "); //file delimeter
			strcat(result_buff, inf);
			strcat(result_buff, " ");
			//print modified date
			//parsing date
			inf = ctime(&st.st_mtime); //get date, month, day, hour:minute:second, year
			inf = strtok(inf, " ");	//date
			inf = strtok(NULL, " ");	//month
			strcat(result_buff, inf);
			strcat(result_buff, " ");
			inf = strtok(NULL, " ");	//day
			for (int i = 0; i < (2 - strlen(inf)); i++) strcat(result_buff, " "); //file delimeter
			strcat(result_buff, inf);
			strcat(result_buff, " ");
			inf = strtok(NULL, ":");	//hour
			strcat(result_buff, inf);
			strcat(result_buff, ":");
			inf = strtok(NULL, ":");	//minute
			strcat(result_buff, inf);
			strcat(result_buff, " ");
		}
		strcat(result_buff, path);
		strcat(result_buff, "\n");
		usleep(10000);
		write(datafd, result_buff, strlen(result_buff));	//send result to client
		usleep(10000);
		return 1;
	} //end of if
	while((dirp = readdir(dp)))  //recursively search files
	{
		if (!(strncmp(dirp->d_name, ".", 1)) && (aflag == 0)) continue;
		fileArr[cnt++] = strdup(dirp->d_name);
	} //end of while
	qsort(fileArr, cnt, sizeof(char*), cmp);	//sorting
	int newline = 0;
	//////////////////////// print directory information //////////////////////
	for (int j = 0; j < cnt; j++)	//print information
	{
		char* filename = (char*)malloc(sizeof(char) * 256);
		strcpy(filename, fileArr[j]);
		result_buff[0] = '\0';
		strcpy(filename, path);
		strcat(filename, "/");
		strcat(filename, fileArr[j]);
		lstat(filename, &st); 	//get file status
		lstat(filename, &st);
		if (lflag)				//lflag enabled
		{
			//print file format
			if (S_ISDIR(st.st_mode)) strcat(result_buff, "d");			//directory
			else if (S_ISFIFO(st.st_mode)) strcat(result_buff, "p");	//FIFO
			else if (S_ISLNK(st.st_mode)) strcat(result_buff, "l");	//link
			else if (S_ISSOCK(st.st_mode)) strcat(result_buff, "s");	//socket
			else if (S_ISBLK(st.st_mode)) strcat(result_buff, "b");	//block
			else if (S_ISCHR(st.st_mode)) strcat(result_buff, "c");	//character device
			else strcat(result_buff, "-");	
			//print authority
			for (int i = 0; i < 3; i++)
			{
				if (st.st_mode & (S_IREAD >> i*3)) strcat(result_buff, "r");	//read
				else strcat(result_buff, "-");								//non
				if (st.st_mode & (S_IWRITE >> i*3)) strcat(result_buff, "w");	//write
				else strcat(result_buff, "-");								//non
				if (st.st_mode & (S_IEXEC >> i*3)) strcat(result_buff, "x");	//execute
				else strcat(result_buff, "-");								//non
			} //end of for
			strcat(result_buff, " ");
			//print the number of link
			inf = Myltoa((long long)st.st_nlink);
			for (int i = 0; i < (3 - strlen(inf)); i++) strcat(result_buff, " "); //file delimeter
			strcat(result_buff, inf);
			strcat(result_buff, " ");
			//print file owner
			strcat(result_buff, getpwuid(st.st_uid)->pw_name);
			strcat(result_buff, " ");
			//print file group name
			strcat(result_buff, getgrgid(st.st_gid)->gr_name);
			strcat(result_buff, " ");
			//print file size
			inf = Myltoa((long long)st.st_size);
			//Right align
			for (int i = 0; i < (FILETAP - strlen(inf)); i++) strcat(result_buff, " "); //file delimeter
			strcat(result_buff, inf);
			strcat(result_buff, " ");
			//print modified date
			//parsing date
			inf = ctime(&st.st_mtime); //get date, month, day, hour:minute:second, year
			inf = strtok(inf, " ");	//date
			inf = strtok(NULL, " ");	//month
			strcat(result_buff, inf);
			strcat(result_buff, " ");
			inf = strtok(NULL, " ");	//day
			for (int i = 0; i < (2 - strlen(inf)); i++) strcat(result_buff, " "); //file delimeter
			strcat(result_buff, inf);
			strcat(result_buff, " ");
			inf = strtok(NULL, ":");	//hour
			strcat(result_buff, inf);
			strcat(result_buff, ":");
			inf = strtok(NULL, ":");	//minute
			strcat(result_buff, inf);
			strcat(result_buff, " ");
		} // end of if
		strcat(result_buff, fileArr[j]); //strcat file name
		if (S_ISDIR(st.st_mode & S_IFMT))	//if file is directory
			strcat(result_buff, "/");	//directory delimeter
		strcat(result_buff, "\n");
		usleep(10000);
		write(datafd, result_buff, strlen(result_buff));
		usleep(10000);
		result_buff[0] = '\0';
		free(fileArr[j]); 					//free memory
		free(filename);
	} //end of for
	//////////////////////// End of print directory information //////////////////////
	closedir(dp); //close directory
	return 1;
}
///////////////////////////////////////////////////////////////////////
// cinsert															//
// =================================================================//
// Input: pid, port													//
// (Input parameter Description) child proccess id and port number	//
// Purpose: insert new child node									//
///////////////////////////////////////////////////////////////////////
void cinsert(int pid, int port)
{
	struct child* pCur = head;
	struct child* newChild = (struct child*)malloc(sizeof(struct child));
	newChild->pid = pid;
	newChild->port = cliaddr.sin_port;
	strcpy(newChild->userID ,username);
	newChild->start = time(NULL);
	newChild->cNext = NULL;
	if (head == NULL) head = newChild;
	else
	{
		while(pCur->cNext) pCur = pCur->cNext;
		pCur->cNext = newChild;
	}
	chnum++;
	return;
}
///////////////////////////////////////////////////////////////////////
// cdelete															//
// =================================================================//
// Input: id														//
// (Input parameter Description) child proccess id					//
// Purpose: delete specific child node								//
///////////////////////////////////////////////////////////////////////
void cdelete(int id)
{
	if (head == NULL)
		return;
	struct child* pCur = head;		//delete target
	struct child* ppCur = NULL;		//target before node
	while(pCur)
	{
		if (pCur->pid == id)
			break;
		ppCur =  pCur;
		pCur = pCur->cNext;
	}
	if (pCur)
	{
		if (pCur == head)
		{
			if (pCur->cNext) head = pCur->cNext;
			else head = NULL;
		}
		else
			if (pCur->cNext) ppCur->cNext = pCur->cNext;
			else ppCur->cNext = NULL;
		// char buf[512];
		// time_t t = time(NULL);
		// sprintf(buf, "%s [127.0.0.1:%d] %s LOG_OUT\n[total service time : %s sec]\n", 
		// ctime(&t), pCur->port, pCur->userID, (ctime(&t) - pCur->start));
		// log_file(buf, 0);
		chnum--;
		free(pCur);
	}
	return;
}
///////////////////////////////////////////////////////////////////////
// cprint															//
// =================================================================//
// Purpose: print child all child node								//
///////////////////////////////////////////////////////////////////////
void cprint()
{
	if (head == NULL)
		return;
	char *buf = (char*)malloc(sizeof(char*) * 256);
	buf[0] = '\0';
	struct child* pCur = head;
	sprintf(buf, "Current Number of Client : %d\n PID\t PORT\t TIME\n", chnum);
	printf("%s", buf);
	time_t current = time(NULL);
	while(pCur)
	{
		buf[0] = '\0';
		sprintf(buf, " %d\t %d\t %ld\t\n", pCur->pid, pCur->port, (current - pCur->start));
		printf("%s", buf);
		pCur = pCur->cNext;
	}
	return;
}

///////////////////////////////////////////////////////////////////////
// sh_chld															//
// =================================================================//
// Input : int seconds												//
// (Input parameter Description) child signal						//
// Purpose: Notify child status change								//
///////////////////////////////////////////////////////////////////////
void sh_chld(int signum)
{
	int chpid = wait(NULL);
	// sprintf(buf, "[total service time : %s sec]\n", (ctime(&t) - pCur->start));
	// log_file(buf, 0);
	// write(STDOUT_FILENO, "Child (", 7);
	// write(STDOUT_FILENO, Myltoa(chpid), strlen(Myltoa(chpid)));
	// write(STDOUT_FILENO, ")'s Release\n", 12);
	cdelete(chpid);
}
///////////////////////////////////////////////////////////////////////
// sh_alrm															//
// =================================================================//
// Input : int seconds												//
// (Input parameter Description) child signal						//
// Purpose: Terminate child process									//
///////////////////////////////////////////////////////////////////////
void sh_alrm(int signum)
{
	alarm(10);
	cprint();
	return;
}
///////////////////////////////////////////////////////////////////////
// client_info														//
// =================================================================//
// Input: struct socket_addr_in	*cliaddr							//
// (Input parameter Description) client socket information struct	//
// Output: 1 : success, -1 : struct is null error					//
// (Out parameter Description) print client success or not			//
// Purpose: Print client IP address and port number					//
///////////////////////////////////////////////////////////////////////
int client_info(struct sockaddr_in *cliaddr)
{
	if (cliaddr == NULL) return -1;
	printf("==========Client info==========\n");
	printf("client IP: %s\n", inet_ntoa(cliaddr->sin_addr));
	printf("client port: %d\n", cliaddr->sin_port);
	printf("===============================\n");
	return 1;
}
///////////////////////////////////////////////////////////////////////
// PrintPid															//
// =================================================================//
// Input: cd, v														//
// (Input parameter Description) command name(cd) 					//
//								child process id(v)					//
// Purpose: print command name and child pid						//
///////////////////////////////////////////////////////////////////////
void PrintPid(char* cd, int v)
{
	char* pid = Myltoa(v);
	write(STDOUT_FILENO, cd, strlen(cd));
	if (strlen(cd) > 7) write(STDOUT_FILENO, "\n\t\t[", 3);
	else write(STDOUT_FILENO, "\t\t\t[", 4);
	write(STDOUT_FILENO, pid, strlen(pid));
	write(STDOUT_FILENO, "]\n", 2);
}
///////////////////////////////////////////////////////////////////////
// sh_int															//
// =================================================================//
// Input: sig														//
// (Input parameter Description) signal number						//
// Purpose: terminate child proccess								//
///////////////////////////////////////////////////////////////////////
void sh_int(int sig)
{
	// char buf[512];
	// printf("Status of Child process was changed.\n");
	// strcpy(buf, "\nChild (");
	// strcat(buf, Myltoa(getpid()));
	// strcat(buf, ")'s Release");
	// write(STDOUT_FILENO, buf, strlen(buf));
	char log_buf[512];
	time_t srvtime = time(NULL);
	/* write log file*/
	log_buf[0] = '\0';
	strcpy(log_buf, ctime(&srvtime));
	log_buf[(strlen(log_buf) - 1)] = '\0';
	if (chnum == 0)
	{
		strcat(log_buf, " Server is terminated\n");
		log_file(log_buf, 0);
		log_buf[0] = '\0';
	}
	printf("\n");
    exit(1);
}