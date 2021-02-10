/**
 * EE 461S: Project 1
 * 
 * Name: Qingyang Hu
 * UTEID: qh2336
 **/


#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <ctype.h>
#include <dirent.h>
#include <dirent.h>
#include <errno.h>


#define MAX_LINE 2000
#define MAX_TOKEN_LEN 30
#define MAX_JOB 20
#define MAX_TOKEN 128



//process
typedef struct Process{
	pid_t pid;		//process ID
	char** command_args;	//the specific command with its args
	int in;			
	int out;
	int error;
	int done;
	int stopped;
	int status;
	struct Process *next;	//might have another process when piping
} Process;

//job(process group)
typedef struct Job{
	pid_t pgid;		//process group ID
	Process *process_list;	//can be one process or two process in pipeline
	struct Job *next;	//next job
	struct Job *previous;	//previous jobsl

	int job_number;		//job number
	int plus_minus;		//+ or -
	char* job_state;	//running, stopped or done
	char* command_line;	//the exact command line
	int isBackground;
	int stop_notification;
} Job;

struct termios yash_tmodes;
//define some global variables
Job* head = NULL;
Job* tail = NULL;
Job* foreground_job = NULL;
pid_t yash_pid; //the process ID of yash program
int total_job_num = 0;
//int DEBUG = 1; //for debugging purposes
int DEBUG = 0;

void extractTokens(char* inString);
void traverseTokens(char* tokens[], int size, char* inString);
void exec_job(Job* current_job, int isPipe, int isBackground);
Process* create_new_process();
Job* create_new_job(char* inString, Process* current_process);
void job_handler();
void foreground_process_handler();
void background_process_handler();
void sig_handler(int signo);
void wait_job_to_complete(Job* current_job);
int traverse_process_status(int pid, int status);
int job_done(Job* job);
int job_stopped(Job* job);
void all_process_running(Job* job);
void delete_job(Job* delete_job);
void update_process_status();
void job_status_printer(Job* job, char* state);
void default_job_notification(int notification);

int main(){
	//ignore for the yash process
   signal (SIGINT, SIG_IGN);
   signal (SIGTSTP, SIG_IGN);
   //signal (SIGINT, sig_int_handler_parent);
   //signal (SIGTSTP, sig_stop_handler_parent);
   signal (SIGQUIT, SIG_IGN);
   signal (SIGTTIN, SIG_IGN);
   signal (SIGTTOU, SIG_IGN);
   signal (SIGCHLD, SIG_IGN);

	//point to sig handler function
    if (signal(SIGCHLD, sig_handler)  == SIG_ERR){
		printf("signal(SIGCHLD) error");
	}

	//create a process group for yash 
	yash_pid = getpid();
	if(setpgid(yash_pid, yash_pid) < 0){
		exit(-1);
	}
	tcsetpgrp(0, yash_pid);
   
    	
	
	char *inString;
	while(inString = readline("# ")){
		//exit when pressing ctl-D
		if(inString == NULL){
			exit(0);
		}

        //extract tokens from the command line
		extractTokens(inString);
	}
}

void extractTokens(char* inString){
	char *tokens[MAX_TOKEN+1]={NULL};
	char *cl_copy, *token, *save_ptr, *to_free, *copy;
        int i;
  	copy = strdup(inString);
	//printf("The inString is %s\n", copy);
        cl_copy = to_free = strdup(inString);

        i = 0;

	//extract token and store into array tokens
        while((token = strtok_r(cl_copy, " ", &save_ptr))){
                 tokens[i] = token;
                 i++;
                 cl_copy = NULL;
        }
	//printf("The inString is %s\n", copy);
	if (DEBUG == 1){
        	i = 0;
        	while(tokens[i]){
                 	printf("token[%d] is %s\n", i, tokens[i]);
                 	i++;
        	}
	}

	
	
	//call traverse token functions
	traverseTokens(tokens, i, copy);

	default_job_notification(1);
	
	free(to_free);
}

void traverseTokens(char* tokens[], int size, char* inString){
	//printf("i am here! 1 \n");
	update_process_status();
	//default_job_notification();
	//create a new process and new job
	Process *current_process = create_new_process();
	char* command;
	if(tail!=NULL){
		command = tail->command_line;
		//printf("haahahahah %s", tail->command_line);
	}
	
	Job* current_job;
	int isPipe = 0;

	if (DEBUG == 1){
        	printf("traverseToken function: the inString is %s\n", inString);
	}
	if(tokens[0]==NULL){
		return;
	}
	// if the commands are fg, bg, and jobs, need to write handler
	if(strcmp(tokens[0], "fg") == 0){
		//printf("i am here! 2\n");
		free(current_process);
		if(size == 1){
			
			//printf("haahahahah %s", tail->command_line);
			if(tail!=NULL){
				//tcsetpgrp(0, yash_pid);
				//printf("haaaaaa!");
				printf("%s\n", command);
				foreground_process_handler();
			}
			return;
		}
	}
	else if(strcmp(tokens[0], "bg") == 0){
		free(current_process);
        if(size == 1){
			if(tail!= NULL){
				if(tail->isBackground == 0){
					printf("[%d]%c %s &", tail->job_number, '+', tail->command_line);

				}
				else{
					printf("[%d]%c %s", tail->job_number, '+', tail->command_line);
				}
            	background_process_handler();
			}
			return;
        }
 
    }
	else if(strcmp(tokens[0], "jobs") == 0){
		free(current_process);
        if(size == 1){
            job_handler();
			default_job_notification(0);
			return;
        }
    }
	
	
	else{
		int cur_arg = 0;
		int isBackground = 0;
		
		for(int i = 0; i < size; i++){ //file redirection and pipe
			if(strcmp(tokens[i], "<") == 0){
				if((i == 0)||(i+1 == size)){
					free(current_process);
					return;
				}
				char* filename = strdup(tokens[i+1]);
				//current_process -> in = open(filename, O_WRONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
				current_process -> in = open(filename, O_RDONLY);
				if (current_process -> in < 0){
					free(current_process);
					return;				
				}
				i++;
			}
			else if(strcmp(tokens[i], ">") == 0){
				if((i == 0)||(i+1 == size)){
					free(current_process);
					return;
				}
				char* filename = strdup(tokens[i+1]);
				current_process -> out = open(filename, O_CREAT|O_TRUNC|O_WRONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
				i++;
			}
			else if(strcmp(tokens[i], "2>") == 0){
				if((i == 0)||(i+1 == size)){
					free(current_process);	
					return;
				}
				char* filename = strdup(tokens[i+1]);
				current_process -> error = open(filename, O_CREAT|O_TRUNC|O_WRONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
				i++;
			}
			else if(strcmp(tokens[i], "|") == 0){ //pipe command
				isPipe = 1;
				Process* first_process = current_process;
				current_job = create_new_job(inString, first_process);
				//create the second process
				current_process = create_new_process();
				cur_arg = 0;
				
			}
			else if(strcmp(tokens[i], "&") == 0){ //background process
				if(i == (size-1)){
					isBackground = 1;
					if(DEBUG == 1){
						printf("A successful background process!\n");
					}
				}
				else{
					free(current_process);
					return;
				}
			}
			else{
				current_process -> command_args[cur_arg] = tokens[i];
				current_process -> command_args[cur_arg+1] = NULL;
				if(DEBUG==1){
					printf("beforePipe: %s\n", current_process->command_args[cur_arg]);
				}
				cur_arg++;
			}
		}

	    
		//create a new job
		if(isPipe == 0){
			if(DEBUG==1){
				printf("No pipe and ready to create new job!\n");
			}
			current_job = create_new_job(inString, current_process);
			if(DEBUG==1){
				printf("Nopipe: The command is %s\n", current_job->process_list->command_args[0]);
			}
		}
		else{
			current_job->process_list->next = current_process;
			if(DEBUG==1){
				printf("Pipe: \nThe first command is %s\n The second command is %s\n", current_job->process_list->command_args[0], current_job->process_list->next->command_args[0] );
			}
		}

		current_job->isBackground = isBackground;
		if(!isBackground){
			foreground_job = current_job;
		}

		exec_job(current_job, isPipe, isBackground);

	}
}

//need to notify user about stopped or terminated job
//1 = want notification, 0 = do not want notification
void default_job_notification(int notification){
	update_process_status();

	Job* current_job;
	for(current_job = head; current_job; current_job= current_job->next){
		//check if the job has been completed
		/* if(job_done(current_job)){
			if(notification==1 && current_job->isBackground==1){
				job_status_printer(current_job, "Done");
			}
			
			//delete the job and free the job from the link list of jobs
			delete_job(current_job);
		}
		//check if the job has been stopped
		//mark them as already notified
		else if(job_stopped(current_job) && current_job->stop_notification != 1){
			if(notification==1){
				job_status_printer(current_job, "Stopped");
				current_job->stop_notification = 1;
			}
		} */

		if(job_done(current_job)){
			if(notification==1 && current_job != foreground_job){
				job_status_printer(current_job, "Done");
			}
			delete_job(current_job);
		}
		else if(job_stopped(current_job) && current_job->stop_notification != 1){
			current_job->stop_notification = 1;
			if(notification){
				//job_status_printer(current_job, "Stopped");
			}
		}
		else{
			
		}
	}
}

void delete_job(Job* current_job){
	total_job_num --;
	Job* delete_job = current_job;
	if(delete_job==head && delete_job==tail){
		free(delete_job);
		head = NULL;
		tail = NULL;
	}
	else if(delete_job == head){
		head = head->next;
		head -> previous = NULL;
		free(delete_job);
	}
	else if(delete_job == tail){
		tail = tail -> previous;
		tail -> next = NULL;
		tail -> plus_minus = '+';
		free(delete_job);
	}
	else{
		delete_job->previous->next = delete_job->next;
		delete_job->next->previous = delete_job->previous;
		free(delete_job);
	}
}

//check processes status in every loop
//do not block anything
void update_process_status(){
	pid_t pid;
	int status;

	pid = waitpid(-1, &status, WUNTRACED|WNOHANG);
	//WNOHANG will return immediately even if no child has exits
	while(!traverse_process_status(pid, status)){
		pid = waitpid(-1, &status, WUNTRACED|WNOHANG);
	}
}

void job_status_printer(Job* curr, char* job_state){
	if (curr == tail->previous){
		printf("[%d] %c %s		%s\n", curr->job_number, '+', job_state, curr->command_line);
	}
	else{
		printf("[%d] %c %s		%s\n", curr->job_number, curr->plus_minus, job_state, curr->command_line);
	}
}

void job_handler(){
	update_process_status();
	Job* curr = head;
	while(curr){
		if(job_done(curr)){
			printf("[%d] %c %s		%s\n", curr->job_number, curr->plus_minus, "Done", curr->command_line);
		}
		else if(job_stopped(curr)){
			printf("[%d] %c %s		%s\n", curr->job_number, curr->plus_minus, "Stopped", curr->command_line);
		}
		else{
			printf("[%d] %c %s		%s\n", curr->job_number, curr->plus_minus, "Running", curr->command_line);
		}
		curr = curr->next;
	}
}

void foreground_process_handler(){ //for handling fg
	if(total_job_num == 0){
		printf("No such job!");
		return;
	}
	//get the recent job
	Job* curr_job = tail;
	//printf("Handling fg!");
	//mark all the processes as running
	all_process_running(curr_job);
	//if job is not done, get the terminal control and send a cont signal to continue the job
	if(job_done(curr_job)==0){
		//printf("Continue the job!");
		//printf("%s", curr_job->command_line);
		tcsetpgrp(0, curr_job->pgid);
		kill(curr_job->pgid, SIGCONT);
		foreground_job = curr_job;
		curr_job->job_state = "Running";
		curr_job->isBackground=0;

		wait_job_to_complete(curr_job);
		tcsetpgrp(0, yash_pid);
	}
	else{
		return;
	}
}

void background_process_handler(){ //for handling bg
	if(total_job_num == 0){
		//printf("No such job!");
		return;
	}
	//get the recent job
	Job* curr_job = tail;
	//printf("Handling bg!");
	//mark all the processes as running
	all_process_running(curr_job);
	//if job is not done, get the terminal control and send a cont signal to continue the job
	if(job_done(curr_job)==0){
		//printf("Continue the job!");
		//tcsetpgrp(0, curr_job->pgid);
		kill(curr_job->pgid, SIGCONT);
		curr_job->job_state = "Running";
		/* if(curr_job->isBackground == 0){
			printf("[%d]%c %s &", curr_job->job_number, '+', curr_job->command_line);
		}
		else{
			printf("[%d]%c %s", curr_job->job_number, '+', curr_job->command_line);
		} */
		curr_job->isBackground = 1;
		
	}
	else{
		return;
	}
}

void all_process_running(Job* job){
	Process* curr_p;
	for(curr_p = job->process_list; curr_p; curr_p = curr_p->next){
		curr_p->stopped = 0;
	}
}

//int process_debug = 0;
int process_debug = 1;

void exec_job(Job* current_job, int isPipe, int isBackground){
	if (isPipe){ //two commands, launch the process, will wait until finish if isBackground
				//need to create process group and process when they launch
		int status, done = 0;
		pid_t cpid;	
		Process* first_process = current_job -> process_list;
		Process* second_process = current_job -> process_list -> next;
		
		//printf("first process is %s, second process is %s", first_process->command_args[0], second_process->command_args[0]);
		int pfd[2];
		if(pipe(pfd)<0){
			exit(-1);
		}

		if(first_process -> out == 1){
			first_process -> out = pfd[1];
		}
		if(second_process -> in == 0){
			second_process -> in = pfd[0];
		}
		//printf("before fork");
		cpid = fork();
		//printf("Inside pipe: %d, %d\n", pfd[0], pfd[1]);
		if(cpid == 0){ //first child
		if(process_debug==1){ //set process group
			//printf("First child\n");
			first_process->pid = getpid();
			if(current_job->pgid==0){
				current_job->pgid = first_process->pid;
			}
			setpgid(first_process->pid, current_job->pgid);
		}

			signal (SIGINT, SIG_DFL);
   			signal (SIGQUIT, SIG_DFL);
   			signal (SIGTSTP, SIG_DFL);
   			signal (SIGTTIN, SIG_DFL);
   			signal (SIGTTOU, SIG_DFL);
   			signal (SIGCHLD, SIG_DFL);

			if (DEBUG ==1){
				printf("first child!");
				printf("child 1 pid: %d\n", first_process->pid);
			}
			close(pfd[0]);
			if(first_process->in != 0){
				dup2(first_process->in, 0);
				close(first_process->in);
			}
		
			if(first_process->out != 1){
				dup2(first_process->out, 1);
				close(first_process->out);
			}
			
			if(first_process->error != 2){
				dup2(first_process->error, 2);
				close(first_process->error);
			}
		
			execvp(first_process->command_args[0], first_process->command_args);
			if(DEBUG == 1){
				printf("Child fail, force to exit!");
			}			
			exit(-1);
		}

		if(DEBUG==1){
			printf("pid1: %d\n", cpid);
		}

		if(process_debug==1){
			//printf("First parent\n");
			first_process->pid = cpid;
			if(current_job->pgid==0){
				current_job->pgid = first_process->pid;
			}
			setpgid(first_process->pid, current_job->pgid);
		}

		cpid = fork();
		if(cpid == 0){ //second child
			
			if(process_debug==1){
				//printf("Second child\n");
				second_process->pid = getpid();
				if(current_job->pgid==0){
					current_job->pgid = second_process->pid;
				}
				setpgid(second_process->pid, current_job->pgid);
			}
			if (DEBUG ==1){
				printf("second child!");
				printf("child 2 pid: %d\n", second_process->pid);
			}

			signal (SIGINT, SIG_DFL);
   			signal (SIGQUIT, SIG_DFL);
   			signal (SIGTSTP, SIG_DFL);
   			signal (SIGTTIN, SIG_DFL);
   			signal (SIGTTOU, SIG_DFL);
   			signal (SIGCHLD, SIG_DFL);
			
			close(pfd[1]);
			if(second_process->in != 0){
				dup2(second_process->in, 0);
				close(second_process->in);
			}
		
			if(second_process->out != 1){
				dup2(second_process->out, 1);
				close(second_process->out);
			}
	
			if(second_process->error != 2){
				dup2(second_process->error, 2);
				close(second_process->error);
			}
		
			execvp(second_process->command_args[0], second_process->command_args);
			if(DEBUG == 1){
				printf("Child fail, force to exit!");
			}			
			exit(-1);
		}

		if(DEBUG==1){
			printf("pid2: %d\n", cpid);
		}

		if(process_debug==1){
			//printf("Second parent\n");
			second_process->pid = cpid;
			if(current_job->pgid==0){
				current_job->pgid = second_process->pid;
			}
			setpgid(second_process->pid, current_job->pgid);
		}


		close(pfd[0]);
		close(pfd[1]);

		if(isBackground==0){
			waitpid(-1, &status, 0);
			waitpid(-1, &status, 0);
		}

		//after two child both finished
		if(DEBUG==1){
			printf("Both child has finished!");
			printf("%d, %d\n", first_process->pid, second_process->pid);
			printf("first process is %s, second process is %s", first_process->command_args[0], second_process->command_args[0]);
		}

		//current_job->pgid = first_process->pid;
		if(DEBUG==1){
			printf("The process group ID is %d, child 1 pid is %d, child 2 pid is %d\n", current_job->pgid, first_process->pid,second_process->pid);
		}
		//setpgid(first_process->pid, current_job->pgid);
		//setpgid(second_process->pid, current_job->pgid);
		if(DEBUG==1){
			printf("The process group ID is %d, child 1 pid is %d, child 2 pid is %d\n", current_job->pgid, first_process->pid,second_process->pid);
		}
	}
	else{ //only one command

		int status, done = 0;
	
		pid_t cpid;	
		Process* first_process = current_job -> process_list;
		cpid = fork();
		if (cpid < 0){
			exit(-1);
		}
		else if (cpid == 0){
			first_process->pid = getpid();
			current_job->pgid = first_process->pid;
			setpgid(first_process->pid, current_job->pgid);
			if(isBackground == 0){ //foreground
				tcsetpgrp(0, current_job->pgid); //child take control of the terminal
				if (DEBUG == 1){
					printf("child takes control of the terminal!");
				}
			}
			
			//restore child signal behavior
			signal (SIGINT, SIG_DFL);
   			signal (SIGQUIT, SIG_DFL);
   			signal (SIGTSTP, SIG_DFL);
   			signal (SIGTTIN, SIG_DFL);
   			signal (SIGTTOU, SIG_DFL);
   			signal (SIGCHLD, SIG_DFL);

			if(first_process->in != 0){
				dup2(first_process->in, 0);
				close(first_process->in);
			}
		
			if(first_process->out != 1){
				dup2(first_process->out, 1);
				close(first_process->out);
			}
			
			if(first_process->error != 2){
				dup2(first_process->error, 2);
				close(first_process->error);
			}
			
			execvp(first_process->command_args[0], first_process->command_args);
			if(DEBUG == 1){
				printf("Child fail, force to exit!");
			}			
			exit(-1);
		}
		else{ //parent process

			first_process->pid = cpid;
			current_job->pgid = first_process->pid;
			setpgid(first_process->pid, current_job->pgid);


			//set one process group and add child process to the group
			//now all process has been launched
			if(isBackground==0){ //foreground
				tcsetpgrp(0, current_job->pgid); //control to current job
				if (DEBUG==1){
					printf("Job is foreground, Before wait_to_complete!\n");
				}
				wait_job_to_complete(current_job);
				tcsetpgrp(0, yash_pid);	//parent takes back the control of terminal
			}
			else{ //background
				tcsetpgrp(0, yash_pid); //yash takes control back
			}
			
			if(DEBUG==1){
				printf("pgid is %d, pid is %d, is it done? :%d, is it stopped? :%d\n", current_job->pgid, current_job->process_list->pid, current_job->process_list->done, current_job->process_list->stopped);
				//printf("pgid is %d, pid is %d", current_job->pgid, first_process->pid);
			}
		}

	}

}

void wait_job_to_complete(Job* current_job){
	pid_t pid;
	int status;

	//need to wait for all children either stopped or terminated
	pid = waitpid(-1, &status, WUNTRACED); //return when child has completed or has stopped by signal
											//return the pid of the child
											//return the status of the child
	while(!traverse_process_status(pid, status) && !job_stopped(current_job) && !job_done(current_job)){
		pid = waitpid(-1, &status, WUNTRACED);
	}
	if(DEBUG==1){
		printf("The job is complete!\n");
	}
}

//return 0 if every process if checked
//update if the process has been stopped or completed
int traverse_process_status(int pid, int status){
	Job* current_job;
	Process* current_process;
	if(pid > 0){
		for(current_job = head; current_job; current_job=current_job->next){
			for(current_process = current_job->process_list;current_process;current_process = current_process->next){
				if(pid == current_process->pid){
					current_process->status = status;
					if(WIFSTOPPED(status)){
						current_process->stopped = 1;
					}
					else{
						current_process->done = 1;
						if(WIFSIGNALED(status)){
							if(DEBUG==1){
								printf("terminated by signal!\n");
							}
						}
					}
					return 0;
				}
			}
		}
	}
	else if(pid == 0){
		return -1;
	}
	else{
		return -1;
	}
}

//check if all processes has been complete
int job_done(Job* current_job){
	Process* curr_p;
	for(curr_p = current_job->process_list;curr_p;curr_p=curr_p->next){
		if(curr_p->done == 0){
			return 0;
		}
	}
	current_job->job_state = "Done";
	return 1;
}

//check if all processes has been stopped
int job_stopped(Job* current_job){
	Process* curr_p;
	for(curr_p = current_job->process_list;curr_p;curr_p=curr_p->next){
		if(curr_p->stopped == 0){
			return 0;
		}
	}
	current_job ->job_state = "Stopped";
	return 1;
}


void sig_handler(int signo) {
   switch(signo){
      case SIGCHLD:
	  		//printf("Parent caught child either stop or terminate!");
      break;
   }
}

Process* create_new_process(){
    //create a process
    Process *current_process = (Process *) malloc(sizeof(Process));
    current_process -> command_args = (char **) malloc(MAX_TOKEN+1);
	current_process -> command_args[0] = NULL;
    current_process -> in = 0;
    current_process -> out = 1;
    current_process -> error = 2;
	current_process -> next = NULL; 
	current_process -> pid = 0; 
	current_process -> done = 0;
	current_process -> stopped = 0;

	return current_process;
}

Job* create_new_job(char* inString, Process* current_process){
        //create a job
    Job *current_job = (Job *)malloc(sizeof(Job));
	if (head == NULL){ //the first job
		head = current_job;
		tail = current_job;
		current_job -> next = NULL;
		current_job -> previous = NULL;
		current_job -> job_number = 1;
	}
	else{ //not the first job
		tail -> next = current_job;
		current_job -> previous = tail;
		current_job -> next = NULL;
		current_job -> job_number = tail -> job_number + 1;
		tail -> plus_minus = '-';		
		tail = current_job;
	}

	current_job -> process_list = current_process;
	current_job -> plus_minus = '+';
	current_job -> job_state = "Running";
	current_job -> command_line = inString;
	current_job -> pgid = 0;
	current_job -> isBackground = 0;
	current_job -> stop_notification = 0;
	total_job_num ++;
	return current_job;

}
