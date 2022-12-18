#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

char buffer[1000];
char *seperatedBuffer[100];
char *history[100];
char *commandList[10];
int commandNum = 6;
int commandLen;
int historyLen = -1;
//handling signal
int pid = 0;
int exitCode = 0;
//for I/O redirection
int metaHistry = -1;
int metaPos = -1;
int fd;
int temp;
void ioRedirection(char** command, int len);

//for job control
typedef struct background{
    int jobid;
    char *cmd[100];
    int cmdLen;
    int pid;
    char *status;
    //fg = 0, bg = 1
    int fgbg;
    int notified;
} job;
job jobs[1000];
int jobnum = 0;

void echo(char** command, int n) {

    if(n == 2 && !(strcmp(command[1], "$?"))) {
        printf("%d\n", exitCode);
        return;
    }

    for (int i=1; i<n; i++) {
        printf("%s", command[i]);
        if (i < n-1) printf(" ");
    }

    printf("\n");

}

void addHistory(char** command, int n) {

    for (int i =0; i< historyLen; i++) {
        free(history[i]);
        history[i] = NULL;
    }

    for (int i=0; i<n; i++) {
        history[i] = strdup(command[i]);
    }

    historyLen = n;

}

void createCommand(char** list) {

    list[0] = "echo";
    list[1] = "!!";
    list[2] = "exit";
    list[3] = "jobs";
    list[4] = "fg";
    list[5] = "bg";

}

void seperateInput(char* command, char** output) {

    char* words = strtok(command, " ");
    int len;

    int i = 0;
    while (words != NULL) {
        //use for I/O redirection
        if (!strcmp(words, "<") || !strcmp(words, ">")) metaPos = i;

        output[i++] = words;
        words = strtok(NULL, " ");
    }
    len = i;

    //get rid of '\n' from the last argument
    int lastchar = strlen(output[len-1]) - 1;
    output[len-1][lastchar] = '\0';
    if (lastchar == 0) {
        output[len-1] = '\0';
        len--;
    }

    //clear the old remaining commands
    while(i < historyLen) {
        output[i++] = NULL;
    }

    commandLen = len;
}

void printJobInfo(char **command, int len, int jid) {

	printf("[%d]   %s                    "
            , jobs[jid].jobid, jobs[jid].status);
    for (int i=0; i<len; i++) {
        printf("%s ", command[i]);
    }

}

void clearDoneJob() {

    //notify completed jobs
    for (int jid=0; jid<jobnum; jid++) {
    	int stopped;
    	int status;
    	//alive = 0, done != 0
    	int done = waitpid(jobs[jid].pid, &status, WNOHANG | WUNTRACED);
    	stopped = WIFSTOPPED(status);

        if (done && !stopped && !jobs[jid].notified) {
        	if (jobs[jid].fgbg) {
        		jobs[jid].notified = 1;
        		jobs[jid].status = "Done";
        		printJobInfo(jobs[jid].cmd, jobs[jid].cmdLen ,jobs[jid].jobid-1);
        		printf("\n");
        	}
        	for (int i=0; i<jobs[jid].cmdLen; i++) {
        		free(jobs[jid].cmd[i]);
        	}
        }
    }

	//reset job id
    if (jobnum == 0) return;

    for (int j=jobnum-1; j>=0; j--) {
    	int stopped;
    	int status;
        //alive = 0, done != 0
        int done = waitpid(jobs[j].pid, &status, WNOHANG | WUNTRACED);
        stopped = WIFSTOPPED(status);

        if (j==0) {
       		if (done) {
                jobnum = 0;
                break;
            }
        }
        if (!done || stopped) {
            jobnum = j+1;
            break;
        }
    }
}


void listjob() {

    for (int jid=0; jid<jobnum; jid++) {
    	if (!waitpid(jobs[jid].pid, 0, WNOHANG)) {
    		printf("[%d]  %s                    ",
                jobs[jid].jobid, jobs[jid].status);
    		for (int i=0; i<jobs[jid].cmdLen; i++) {
    			printf("%s ", jobs[jid].cmd[i]);
    		}
			if (!strcmp(jobs[jid].status, "Running")) printf(" &");
			printf("\n");
    	}
    }
}

void bringtofg(char *charjid) {

	//get the job id
	int len = strlen(charjid);
	for (int i=0; i<len-1; i++) {
		charjid[i] = charjid[i+1];
	}
	charjid[len-1] = '\0';
	int jid = atoi(charjid);
	jid--;

	//print its command
	for (int i=0; i<jobs[jid].cmdLen; i++) {
		printf("%s ", jobs[jid].cmd[i]);
	}
	printf("\n");

	//continue the process
	jobs[jid].fgbg = 0;
	pid = jobs[jid].pid;
	jobs[jid].status = "Running";
	kill(jobs[jid].pid, SIGCONT);

	int status;
	int stopped;
	waitpid(jobs[jid].pid, &status, WUNTRACED);
	stopped = WIFSTOPPED(status);
	if (stopped) printf("\n");
	exitCode = WEXITSTATUS(status);

}

void bringtobg(char *charjid) {
	//get the job id
	int len = strlen(charjid);
	for (int i=0; i<len-1; i++) {
		charjid[i] = charjid[i+1];
	}
	charjid[len-1] = '\0';
	int jid = atoi(charjid);
	jid--;

	printf("[%d]   ", jobs[jid].jobid);
	for (int i=0; i<jobs[jid].cmdLen; i++) {
		printf("%s ", jobs[jid].cmd[i]);
	}
	printf("&\n");

	jobs[jid].status = "Running";
	jobs[jid].fgbg = 1;
	kill(jobs[jid].pid, SIGCONT);
}

int foreground(char** command, int len) {

    int status = 0;

    //set last element to NULL
    for (int i=0; i<len; i++) {
        if (command[i][0] == '\0') command[i] = NULL;
    }
    command[len] = NULL;

    if ((pid=fork()) < 0) {
        perror ("Fork failed");
        exit(errno);
    }
    if (!pid) {
        if (metaPos != -1) {
            if (!strcmp(seperatedBuffer[metaPos], ">")) dup2(fd, 1);
            else if (!strcmp(seperatedBuffer[metaPos], "<")) dup2(fd, 0);
        }
        execvp(command[0], command);
        //end process in case of unsupported command
        exit(-1);
    }

    if (pid) {
        waitpid(pid, &status, 0);
        exitCode = WEXITSTATUS(status);
    }
        return status;
}

void background(char **command, int len) {

    int jid = jobnum++;
    jobs[jid].jobid = jobnum;

    //set last element to NULL and remove '&'
    for (int i=0; i<len; i++) {

        char* temp = command[i];
        int tempLen = strlen(temp);
        if (temp[tempLen-1] == '&')
            temp[tempLen-1] = '\0';

        if (command[i][0] == '\0' || command[i][0] == '&') {
            command[i] = NULL;
            jobs[jid].cmd[i] = '\0';
            len--;
            continue;
        }
        jobs[jid].cmd[i] = strdup(command[i]);

    }
    jobs[jid].cmdLen = len;
    command[len] = NULL;

    if ((pid=fork()) < 0) {
        perror ("Fork failed");
        exit(errno);
    }
    if (!pid) {
         execvp(command[0], command);
    }

    if (pid) {
        printf("[%d] %d\n", jobnum, pid);
        jobs[jid].pid = pid;
        pid = 0;
        jobs[jid].status = "Running";
        jobs[jid].fgbg = 1;
        jobs[jid].notified = 0;
    }
        return ;
}


//update: 0 = don't update history, 1 = update history
void processInput(char** command, int wordCount, int update) {

    //when called '!!' and there is no last command
    if ((strcmp("!!", command[0]) == 0) && historyLen == -1) return;

    int commandID = 0;
    for (int i=0; i<commandNum; i++) {
        if (strcmp(command[0], commandList[i]) == 0) {
            commandID = i+1;
            break;
        }
    }

    //looking for '&'
    char *temp = command[wordCount-1];
    if (temp[strlen(temp)-1] == '&') {
        commandID = 7;
    }

    int status;
    switch (commandID) {
    //default
    case 0:
        status = foreground(command, wordCount);
        //if child process was interrupted errno = EINTR
        if (errno == EINTR) printf("\n");
        if (status && errno != EINTR && exitCode == 255 && strcmp(command[0], "./hello")) printf("bad command\n");

        //reset errno (so that it won't affect the prompt loop)
        errno = 0;
        break;
    //command = 'echo'
    case 1:
        echo(command, wordCount);
        exitCode = 0;
        break;
    //command = '!!'
    case 2:
        if (metaHistry < historyLen && metaHistry != -1) {
            if (!strcmp(history[metaHistry], "<") || !strcmp(history[metaHistry], ">")) {
                metaPos = metaHistry;
                ioRedirection(history, historyLen);
                return;
            }
        }
        processInput(history, historyLen, 0);
        exitCode = 0;
        return;
    //command = 'exit'
    case 3:
        printf("goodbye:)\n");
        //free all history
        for (int i=0; i<historyLen; i++) {
            free(history[i]);
        }
        exit(atoi(command[1]) & 0xff);
    //command = 'jobs'
    case 4:
        listjob();
        break;
    //command = 'fg'
    case 5:
    	bringtofg(command[1]);
    	return;
    //command = 'bg'
    case 6:
    	bringtobg(command[1]);
    	return;
    //background job
    case 7:
        background(command, wordCount);
        return;
    }

    if (update == 1)
    addHistory(command, wordCount);
}


void ioRedirection(char** command, int len) {
    char *file1[10];
    char *file2 = command[metaPos+1];
    int f1Len = 0;

    for (f1Len=0; f1Len<metaPos; f1Len++) {
        file1[f1Len] = command[f1Len];
    }

    //out redirect
    if (!strcmp(command[metaPos], ">")) {
        temp = dup(1);
        fd = open(file2, O_TRUNC | O_CREAT | O_WRONLY, 0666);

        if (fd <= 0) {
            fprintf(stderr, "Couldn't open a file\n");
            exit(errno);
        }

        dup2(fd, 1);
        processInput(file1, f1Len, 0);
        dup2(temp, 1);

    }

    //in redirect
    if (!strcmp(command[metaPos], "<")) {
        temp = dup(0);
        fd = open(file2, O_RDONLY);

        if (fd <= 0) {
            fprintf(stderr, "Couldn't open a file\n");
            exit(errno);
        }

        foreground(file1, f1Len);

    }

    close(fd);

    metaHistry = metaPos;
    metaPos = -1;

    //update history only when command is not "!!"
    if (strcmp(seperatedBuffer[0], "!!"))
    addHistory(command, len);
    return;
}

void signalHandler (int sig, siginfo_t *sip, void *notused) {


    if (pid != 0) {
        kill(pid, sig);

		if (sig == SIGTSTP || sig == SIGSTOP) {
			int exist = 0;
			int jid;
			for (int i=0; i<jobnum; i++) {
				if (jobs[i].pid == pid) {
					exist = 1;
					jid = jobs[i].jobid-1;
				}
			}
			if (exist) {
				jobs[jid].status = "Stopped";
			}
			else {
				jid = jobnum++;
				jobs[jid].jobid = jobnum;
				jobs[jid].status = "Stopped";
				jobs[jid].pid = pid;
				jobs[jid].cmdLen = commandLen;

				for (int i=0; i<commandLen; i++) {
					jobs[jid].cmd[i] = strdup(seperatedBuffer[i]);
				}

			}
				errno = 0;
				printf("\n");
				printJobInfo(jobs[jid].cmd, jobs[jid].cmdLen, jid);
		}

        pid = 0;
    }

   return;

}

void createSigHandler() {
    struct sigaction action;
    action.sa_sigaction = signalHandler;
    sigfillset (&action.sa_mask);
    action.sa_flags = SA_SIGINFO;

    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTSTP, &action, NULL);

}

int main(int argc, char** argv) {

    createCommand(commandList);

    createSigHandler();

    //run from script
    if (argc == 2) {
        FILE* fp = fopen(argv[1], "r");

        while(fgets(buffer, 1000, fp)) {
            if (buffer[0] == '\n') continue;

            seperateInput(buffer, seperatedBuffer);
            processInput(seperatedBuffer, commandLen, 1);
        }
        fclose(fp);
        return 0;
    }

    printf("Starting IC shell\n");

    while (1) {
        printf("icsh $ ");
        fgets(buffer, 1000, stdin);
        clearDoneJob();
        if (buffer[0] == '\n' || errno == EINTR) {
            if (errno == EINTR) printf("\n");
            errno = 0;
            continue;
        }

        seperateInput(buffer, seperatedBuffer);
        //I/O redirection
        if (metaPos != -1) {
            ioRedirection(seperatedBuffer, commandLen);
            continue;
        }
        processInput(seperatedBuffer, commandLen, 1);
        errno = 0;
    }
}
