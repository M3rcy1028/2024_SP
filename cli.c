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

#define MAXBUF 1024
#define BUFFSIZE 512

int srvfd;
//declare functions
char* convert_addr_to_str(unsigned long ip_addr, unsigned int port);
int conv_cmd(char *buf, char *sendFTP);
int log_in(int sockfd);
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
    signal(SIGINT, sh_int);
	char *hostport;
    struct sockaddr_in srvaddr;
    int n;
    char buf[MAXBUF];
    /*make control connection*/
	if (argc != 3) 	//argument is missing
	{
		printf("usage : %s <IP> <PORT>\n", argv[0]);
		exit(-1);
	}
    //make server socket
	if ((srvfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		printf("can't create socket.\n");
		exit(-1);
	}
	bzero((char*)&srvaddr, sizeof(srvaddr));	//reset server struct
	srvaddr.sin_family = AF_INET;					//IPv4
	srvaddr.sin_addr.s_addr = inet_addr(argv[1]);	//IP address
	srvaddr.sin_port = htons(atoi(argv[2]));		//port number
    //Control connection
    if(connect(srvfd, (struct sockaddr*)&srvaddr, sizeof(srvaddr)) < 0)
    {
        printf("can't connect.\n");
        exit(-1);
    }
    /////////////////////Access and Authentication////////////////////
    if (log_in(srvfd) == 0)	//function call
        exit(-1);
    /////////////////////FTP command////////////////////
    char* sendFTP = (char*)malloc(sizeof(char*) * 256);
    for(;;)
    {
        usleep(10000);
        strcpy(sendFTP, "\0");
        strcpy(buf, "\0");
        write(STDOUT_FILENO, "ftp> ", 5);
        n = read(STDIN_FILENO, buf, MAXBUF);
        buf[n] = '\0';
        usleep(1000);
        //printf("buf : %s\n", buf);
        if (!strncmp(buf, "quit", 4))
        {
            printf("221 Goodbye.\n");
            write(srvfd, "QUIT", 4);
            usleep(1000);
            break;
        }
        if (conv_cmd(buf, sendFTP) < 0) continue;
        //printf("SENDFTP : %s\n", sendFTP);
        char *databuf = (char*)malloc(sizeof(char*) * 1024);
        databuf[0] = '\0';
        int databuflen = 0;
        /////////////////////make data connection////////////////////
        //make new port randomly for data connection
        //10001 ~ 60000
        if (strstr(sendFTP, "NLST") || strstr(sendFTP, "LIST") || strstr(sendFTP, "RETR") || strstr(sendFTP, "STOR"))
        {
            srand((unsigned int)time(NULL));
            uint16_t dataport = (rand()%5 + 1) * 10000 + rand()%10 * 1000 +
            rand()%10 * 100 + rand()%10 * 10 + rand()%10;
            hostport = convert_addr_to_str(srvaddr.sin_addr.s_addr, dataport);
            //send PORT command
            write(srvfd, hostport, strlen(hostport));
            //receive message from server
            n = read(srvfd, databuf, MAXBUF);
            databuf[n] = '\0';
            printf("%s", databuf);
            //printf("200 Port command performed successful.\n");
            int len, datalen, datafd, connfd;
            struct sockaddr_in dataddr;
            //make socket
            if ((datafd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
            {
                printf("550 Failed to access.\n");
                exit(-1);
            }
            //store client information
            bzero((char*)&dataddr, sizeof(dataddr));
            dataddr.sin_family = AF_INET;					//IPv4
            dataddr.sin_addr.s_addr = htonl(INADDR_ANY);	//IP address
            dataddr.sin_port = htons(dataport);		//port number
            //printf("Data Socket ip : %s, port : %u\n", argv[1], dataport);
            if (bind(datafd, (struct sockaddr *)&dataddr, sizeof(dataddr))< 0)
            {
                printf("550 Failed to access.\n");
                exit(-1);
            }
            listen(datafd, 50);
            datalen = sizeof(dataddr);
            connfd = accept(datafd, (struct sockaddr*)&dataddr, &datalen);
            /////////////////////End of make data connection////////////////////
            //send FTP command
            usleep(1000);
            write(srvfd, sendFTP, strlen(sendFTP));
            databuf[0] = '\0';
            //receive message from server
            n = read(srvfd, databuf, MAXBUF);
            databuf[n] = '\0';
            printf("%s", databuf);
            databuf[0] = '\0';
            //printf("150 Opening data connection for directory list.\n");
            while ((len = read(connfd, databuf, sizeof(databuf))) > 0)
            {
                databuf[len] = '\0';
                if (!strncmp(databuf, "Finished", 8))
                {
                    databuf[0] = '\0';
                    //printf("226 Complete transmission.\n");
                    //receive message from server
                    n = read(srvfd, databuf, MAXBUF);
                    databuf[n] = '\0';
                    printf("%s", databuf);
                    if (!strstr(databuf, "550"))    //error message
                        printf("OK. %d bytes is received.\n", databuflen);
                    databuf[0] = '\0';
                    break;
                }
                else
                {
                    databuflen += strlen(databuf);
                    printf("%s", databuf);
                    databuf[0] = '\0';
                }
            }
            close(datafd);
            buf[0] = '\0';
        }
        else //if (strstr(sendFTP, "PWD"))
        {
            //send FTP command
            write(srvfd, sendFTP, MAXBUF);
            while ((n = read(srvfd, databuf, MAXBUF)) > 0)
            { 
                if (strstr(databuf, "finished!"))
                    break;
                databuf[n] = '\0';
                printf("%s\n", databuf);
                databuf[0] = '\0';
            }
        }
        usleep(10000);
    }
    close(srvfd);
    return;
}
///////////////////////////////////////////////////////////////////////
// conv_cmd														  	 //
// ================================================================= //
// Input: sockfd													 //
// (Input parameter Description) server file description			 //
// Purpose: connect to server and perform authentication			 //
///////////////////////////////////////////////////////////////////////
int conv_cmd(char* buf, char *sendFTP)
{
    ////////////////////User command parsing////////////////////
    int gc = 0, c;
    char **gv = (char**)malloc(BUFFSIZE);
    opterr = 0;
    optind = 0;
    buf[strlen(buf) - 1] = '\0';
    char* ptr = strtok(buf, " ");
    while (ptr)
    {
        gv[gc] = ptr;
        //printf("gv[%d] = %s\n",gc, gv[gc]);
        gc++;
        ptr = strtok(NULL, " ");
    }
    if (gc < 1)
    {
        printf("550 Error: Invalid command.\n");
        return -1;
    }
    usleep(10000);
    /////////////////////End of User command parsing////////////////////
    if (!strncmp(gv[0], "quit", 4))
    {
        usleep(1000);
        return -1;
    }
    if (!strcmp(gv[0], "ls"))
    {
        //parsing options
        int aflag = 0, lflag = 0;
        while((c = getopt(gc, gv, "al")) != -1)
        {
            switch (c)
            {
            case 'a':
                aflag = 1;
                break;
            case 'l':
                lflag = 1;
                break;
            case '?':
                opterr = 1;
                break;
            }
        }
        if (opterr)
        {
            printf("550 Error: Invalid option.\n");
            return -1;
        }
        else
        {
            if (aflag && lflag) strcpy(sendFTP, "NLST -al\n");
            else if (aflag) strcpy(sendFTP, "NLST -a\n");
            else if (lflag) strcpy(sendFTP, "NLST -l\n");
            else strcpy(sendFTP, "NLST\n");
            if (gc < 3)  //has no path or option
            {
                //has path
                if ((aflag == 0 && lflag == 0) && gc == 2) strcat(sendFTP, gv[1]);
                else strcat(sendFTP, ".");  //has no path
            }
            else strcat(sendFTP, gv[2]);
        }
    }
    else if (!strcmp(gv[0], "dir"))
    {
        strcpy(sendFTP, "LIST\n");
        //parsing options
        while ((c = getopt(gc, gv, "")) != -1) //if c = -1, parsing is finished
        {
            switch (c)
            {
                default:	//no flag
                    opterr = 1;
                    break;
            } //end of switch (c)
        } //end of while (getopt())
        if (opterr)
        {
            printf("550 Error: Invalid option.\n");
            return -1;
        }
        else
        {
            if (gc == 1) strcat(sendFTP, ".");
            else strcat(sendFTP, gv[1]);
        }
    }
    else if (!strcmp(gv[0], "pwd"))
    {
        //parsing options
        while ((c = getopt(gc, gv, "")) != -1) //if c = -1, parsing is finished
        {
            switch (c)
            {
                default:	//no flag
                    opterr = 1;
                    break;
            } //end of switch (c)
        } //end of while (getopt())
        if (opterr)
        {
            printf("550 Error: Invalid option.\n");
            return -1;
        }
        else if (gc > 2)
        {
            printf("550 Error: Error: argument is not required.\n");
            return -1;
        }
        else strcpy(sendFTP, "PWD\n");
    }
    else if (!strcmp(gv[0], "cd"))
    {
        strcpy(sendFTP, "CWD\n");
        //parsing options
        while ((c = getopt(gc, gv, "")) != -1) //if c = -1, parsing is finished
        {
            switch (c)
            {
                default:	//no flag
                    opterr = 1;
                    break;
            } //end of switch (c)
        } //end of while (getopt())
        if (opterr)
        {
            printf("550 Error: Invalid option\n");
            return -1;
        }
        else
        {
            if ((gv[1] == NULL) || gc > 2)
            {
                printf("550 Error: only one directory path can be processed\n");
                return -1;
            }
            else
            {
                if (!strncmp(gv[1], "..", 2)) strcpy(sendFTP, "CDUP");
                else
                {
                    strcpy(sendFTP, "CWD\n");
                    strcat(sendFTP, gv[1]);
                }
            }
        }
    }
    else if (!strcmp(gv[0], "mkdir"))
    {
        //parsing options
        while ((c = getopt(gc, gv, "")) != -1) //if c = -1, parsing is finished
        {
            switch (c)
            {
                default:	//no flag
                    opterr = 1;
                    break;
            } //end of switch (c)
        } //end of while (getopt())
        if (opterr)
        {
            printf("550 Error: Invalid option.\n");
            return -1;
        }
        else if (gc < 2)
        {
            printf("550 Error: argument is required.\n");
            return -1;
        }
        else 
        {
            strcpy(sendFTP, "MKD");
            int idx = 1;
            while(gv[idx])
            {
                strcat(sendFTP, " ");
                strcat(sendFTP, gv[idx++]);
            }
        }
    }
    else if (!strcmp(gv[0], "rmdir"))
    {
        //parsing options
        while ((c = getopt(gc, gv, "")) != -1) //if c = -1, parsing is finished
        {
            switch (c)
            {
                default:	//no flag
                    opterr = 1;
                    break;
            } //end of switch (c)
        } //end of while (getopt())
        if (opterr)
        {
            printf("550 Error: Invalid option.\n");
            return -1;
        }
        else if (gc < 2)
        {
            printf("550 Error: argument is required.\n");
            return -1;
        }
        else 
        {
            strcpy(sendFTP, "RMD");
            int idx = 1;
            while(gv[idx])
            {
                strcat(sendFTP, " ");
                strcat(sendFTP, gv[idx++]);
            }
        }
    }
    else if (!strcmp(gv[0], "delete"))
    {
        //parsing options
        while ((c = getopt(gc, gv, "")) != -1) //if c = -1, parsing is finished
        {
            switch (c)
            {
                default:	//no flag
                    opterr = 1;
                    break;
            } //end of switch (c)
        } //end of while (getopt())
        if (opterr)
        {
            printf("550 Error: Invalid option.\n");
            return -1;
        }
        else if (gc < 2)
        {
            printf("550 Error: argument is required.\n");
            return -1;
        }
        else 
        {
            strcpy(sendFTP, "DELE");
            int idx = 1;
            while(gv[idx])
            {
                strcat(sendFTP, " ");
                strcat(sendFTP, gv[idx++]);
            }
        }
    }
    else if (!strcmp(gv[0], "rename"))
    {
        //parsing options
        while ((c = getopt(gc, gv, "")) != -1) //if c = -1, parsing is finished
        {
            switch (c)
            {
                default:	//no flag
                    opterr = 1;
                    break;
            } //end of switch (c)
        } //end of while (getopt())
        if (opterr)
        {
            printf("550 Error: Invalid option.\n");
            return -1;
        }
        else if (gc != 3)
        {
            printf("550 Error: two argument are required\n");
            return -1;
        }
        else 
        {
            int idx = 1;
            sprintf(sendFTP, "RNFR %s & RNTO %s", gv[1], gv[2]);
        }
    }
    else if (!strcmp(gv[0], "bin") || (!strcmp(gv[0], "type") && !strcmp(gv[1], "binary")))
    {
        //parsing options
        while ((c = getopt(gc, gv, "")) != -1) //if c = -1, parsing is finished
        {
            switch (c)
            {
                default:	//no flag
                    opterr = 1;
                    break;
            } //end of switch (c)
        } //end of while (getopt())
        if (opterr)
        {
            printf("550 Error: Invalid option.\n");
            return -1;
        }
        else if (gc > 2)
        {
            printf("550 Error: Error: argument is not required.\n");
            return -1;
        }
        else strcpy(sendFTP, "TYPE I\n");
    }
    else if (!strcmp(gv[0], "ascii") || (!strcmp(gv[0], "type") && !strcmp(gv[1], "ascii")))
    {
        //parsing options
        while ((c = getopt(gc, gv, "")) != -1) //if c = -1, parsing is finished
        {
            switch (c)
            {
                default:	//no flag
                    opterr = 1;
                    break;
            } //end of switch (c)
        } //end of while (getopt())
        if (opterr)
        {
            printf("550 Error: Invalid option.\n");
            return -1;
        }
        else if (gc > 2)
        {
            printf("550 Error: Error: argument is not required.\n");
            return -1;
        }
        else strcpy(sendFTP, "TYPE A\n");
    }
    else if (!strcmp(gv[0], "get"))
    {
        //parsing options
        while ((c = getopt(gc, gv, "")) != -1) //if c = -1, parsing is finished
        {
            switch (c)
            {
                default:	//no flag
                    opterr = 1;
                    break;
            } //end of switch (c)
        } //end of while (getopt())
        if (opterr)
        {
            printf("550 Error: Invalid option.\n");
            return -1;
        }
        else if (gc != 2)
        {
            printf("550 Error: Error: only one argument is required.\n");
            return -1;
        }
        else
        {
            strcpy(sendFTP, "RETR ");
            strcat(sendFTP, gv[1]);
        }
    }
    else if (!strcmp(gv[0], "put"))
    {
        //parsing options
        while ((c = getopt(gc, gv, "")) != -1) //if c = -1, parsing is finished
        {
            switch (c)
            {
                default:	//no flag
                    opterr = 1;
                    break;
            } //end of switch (c)
        } //end of while (getopt())
        if (opterr)
        {
            printf("550 Error: Invalid option.\n");
            return -1;
        }
        else if (gc != 2)
        {
            printf("550 Error: Error: only one argument is required.\n");
            return -1;
        }
        else
        {
            strcpy(sendFTP, "STOR ");
            strcat(sendFTP, gv[1]);
        }
    }
    else
    {
        printf("550 Error : Invalid command.\n");
        return -1;
    }
    return 1;
}
///////////////////////////////////////////////////////////////////////
// log_in														  	 //
// ================================================================= //
// Input: sockfd													 //
// (Input parameter Description) server file description			 //
// Purpose: connect to server and perform authentication			 //
///////////////////////////////////////////////////////////////////////
int log_in(int sockfd)
{
	int n, a, b;	//length of server response, username, password
	int MAX_BUF = 128;
    char user[MAX_BUF], *passwd, buf[512];
	/*check if the ip is acceptable*/
	n = read(sockfd, buf, MAX_BUF);	//ip access response
	buf[n] = '\0';
    printf("%s", buf);
    buf[0] = '\0';
    n = read(sockfd, buf, MAX_BUF);	//ip access response
	buf[n] = '\0';
    printf("%s", buf);
	if (!strncmp(buf, "220", 3))			//ip accepted
	{
		//printf("** It is connected to Server **\n");
		for(;;)
		{
			//reset buffer
			user[0] = '\0';
			buf[0] = '\0';
			//read user input
			write(STDOUT_FILENO, "Name : ", 7);
			n = read(STDIN_FILENO, user, MAX_BUF);
            user[n] = '\0';
            strcpy(buf, "USER ");
            strcat(buf, user);
            //printf("%s", buf);
			write(sockfd, buf, strlen(buf) - 1);
            buf[0] = '\0';
            //receive message
            n = read(sockfd, buf, MAX_BUF);
            buf[n] = '\0';
            printf("%s", buf);
            buf[0] = '\0';
			//write(STDOUT_FILENO, user, strlen(user));
			passwd = getpass("Password : ");
			//write(STDOUT_FILENO, passwd, strlen(passwd));
            strcpy(buf, "PASS ");
            strcat(buf, passwd);
			write(sockfd, buf, strlen(buf));
            buf[0] = '\0';
			n = read(sockfd, buf, MAX_BUF);
			buf[n] = '\0';
			if (!strncmp(buf, "230", 3))				//log-in success
			{
				printf("%s", buf);
				break;
			}
			else if (!strncmp(buf, "430", 3))		//log-in fail
			{
				printf("%s", buf);
			}
			else
			{
				/*three times fail*/
                printf("%s", buf);
				return 0;
			}
		}
	}
	else		//ip denied
    {
        printf("%s", buf);
        return 0;
    }
	return 1;
}
///////////////////////////////////////////////////////////////////////
// convert_addr_to_str												//
// =================================================================//
// Input: unsigned long ip_addr, unsigned int port					//
// (Input parameter Description) ip address and port number and		//
//								paste next to PORT command  		//
// Output: addr : success, NULL : error detected			    	//
// (Out parameter Description) return PORT command          		//
//							if PORT command is failed, return NULL	//
// Purpose: create PORT command with ip address and port number		//
///////////////////////////////////////////////////////////////////////
char* convert_addr_to_str(unsigned long ip_addr, unsigned int port)
{
    char *addr = (char*)malloc(sizeof(MAXBUF)); //PORT command buffer
    strcpy(addr, "PORT ");
    for (int i = 0; i < 4; i++)                 //get IP address
        sprintf(addr, "%s%u,", addr, ((char*)&ip_addr)[i]);
    sprintf(addr, "%s%u,%u", addr, ((port >> 8) & 0xFF), (port & 0xFF));   //get port number
    printf("converting to %s\n", addr + 5);
    return addr;
}

void sh_int(int sig)
{
	write(srvfd, "QUIT", MAXBUF);
    printf("\n221 Goodbye.\n");
    exit(1);
}