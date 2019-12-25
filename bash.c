#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#define true 1
#define false 0

char** arguments(char* command){
	int argc = 0;
	char** argv = calloc(1, sizeof(char*));
	char* buffer = calloc(64, sizeof(char));

	//Ensure that everything is not NULL
	if(!argv || !buffer){
		perror("ERROR: calloc failed");
		free(buffer);
		free(argv);
		return NULL;
	}

	char c;
	do {
		c = *(command++);
		if(isspace(c) || !c) {
			if(strlen(buffer)){
#ifdef DEBUG_MODE
				fprintf(stderr, "Writing to args: \"%s\"\n", buffer);
#endif
				argv[argc] = calloc(strlen(buffer)+1, sizeof(char));
				if(!argv[argc]){
					perror("ERROR: calloc failed");
					for(int i = 0; i < argc; i++) free(argv[argc]);
					free(argv);
					free(buffer);
					return NULL;
				}
#ifdef DEBUG_MODE
				fprintf(stderr, " -> Calloc'd an entry in argv of size %ld\n", strlen(buffer)+1);
#endif
				strcpy(argv[argc], buffer);
#ifdef DEBUG_MODE
				fprintf(stderr, " -> Copied string from buffer to entry\n");
#endif
				argc++;
				argv = realloc(argv, (argc + 1) * sizeof(char*));
				if(!argv) {
					perror("ERROR: realloc failed");
					free(buffer);
					return NULL;
				}
#ifdef DEBUG_MODE
				fprintf(stderr, " -> Realloc'd argv to be one size larger\n");
#endif
				argv[argc] = NULL;
			}
			for(int i = 0; i < 64; i++) buffer[i] = '\0';
		} else buffer[strlen(buffer)] = c;
	} while(c);
	free(buffer);
#ifdef DEBUG_MODE
	for(int i = 0; argv[i]; i++) 
		fprintf(stderr, "Argv[%d]: \"%s\"\n", i, argv[i]);
#endif
	return argv;
}


int get_path(char* command, char* path) {
	char* mypath_env = getenv("MYPATH");
	if(!mypath_env) mypath_env = "/bin#.";
	int i = 0;
	int j = 0;
#ifdef DEBUG_MODE
	fprintf(stderr, "Finding the executable for \"%s\"\n", command);
#endif
	if(command[0] == '/') {
		strcpy(path, command);
		return true;
	}
	do {
		if(mypath_env[i] == '\0' || mypath_env[i] == '#') {
			path[j] = '/';
			struct stat statbuf;
			strcpy(path + j + 1, command);
#ifdef DEBUG_MODE
			fprintf(stderr, " -> Trying \"%s\"\n", path);
#endif
			if(lstat(path, &statbuf) == 0){
#ifdef DEBUG_MODE
				fprintf(stderr, " -> Found \"%s\"\n", path);
#endif
				return true;
			}
			j = 0;
			for(int k = 0; k < strlen(path); k++) path[k] = '\0';
		} else {
			path[j] = mypath_env[i];
			j++;
		}
		i++;
	} while(i <= strlen(mypath_env));
	return false;
}

int run_command(char** args, int infile, int outfile, int background, int pfd[2]){
	char* path = calloc(64, sizeof(char));
	if(!get_path(args[0], path)){
		fprintf(stderr, "ERROR: command \"%s\" not found\n", args[0]);
		free(path);
		return 0;
	}
#ifdef DEBUG_MODE
	fprintf(stderr, "Forking...\n");
#endif
	pid_t child_pid = fork();
	if(child_pid == -1){
		perror("ERROR: fork failed");
		free(path);
		return -1;
	} else if(!child_pid) {
		dup2(infile, 0);
		if(infile == 0 && pfd) close(pfd[0]);
		dup2(outfile, 1);
		if(outfile == 1 && pfd) close(pfd[1]);
#ifdef DEBUG_MODE
		fprintf(stderr, "Input comes from file descriptor %d\n", infile);
		fprintf(stderr, "Output goes to file descriptor %d\n", outfile);
		fprintf(stderr, "Running %s:\n", path);
#endif
		execv(path, args);

		//Error on failure to run command
		free(path);
		fprintf(stderr, "ERROR: ");
		perror(args[0]);
		return -1;
	} else {
		free(path);
		if(pfd && infile == pfd[0]) close(infile);
		if(pfd && outfile == pfd[1]) close(outfile);
		if(background) 
			printf("[running background process \"%s\"]\n", args[0]);
		else {
#ifdef DEBUG_MODE
			fprintf(stderr, "Waiting for process \"%s\" to complete\n", args[0]);
#endif
			waitpid(child_pid, NULL, 0);
		}
#ifdef DEBUG_MODE
		fprintf(stderr, "Process %d terminated\n", child_pid);
#endif
	}

	return 0;
}

int main() {
	setvbuf(stdout, NULL, _IONBF, 0);

	char* command_line = calloc(1024, sizeof(char));
	int running_processes = 0;
	int exit_code = -1;
	char* current_directory = calloc(256, sizeof(char));
	while(true) {
		int* pipefd = NULL;
		int background_processing = false;

		// Write out the terminal prompt.
		getcwd(current_directory, 256);
		printf("%s$ ", current_directory);

		//Get the terminal command
		fgets(command_line, 1024, stdin);
		
		//Parse the terminal command
		char** argv = arguments(command_line);
		char** argv2 = NULL;
		if(!argv) {
			exit_code = EXIT_FAILURE;
			goto cleanup;
		}
		
		//Ignore if the user passes in an empty string
		if(!argv[0]) goto cleanup;

		//If the command is "exit", exit
		if(!strcmp(argv[0], "exit")) {
			printf("bye\n");
			exit_code = EXIT_SUCCESS;
			goto cleanup;
		}

		//If the command is "cd", we are changing the directory
		if(!strcmp(argv[0], "cd")) {
			char* new_directory = argv[1] ? argv[1] : getenv("HOME");
#ifdef DEBUG_MODE
			fprintf(stderr, "Attempting to change directory to %s\n", new_directory);
#endif
			if(chdir(new_directory) == -1) 
				perror("ERROR: Could not switch directory");

			goto cleanup;
		}
		
		//Check for piping and background processing
		for(char** check = argv; *check; check++) {
			if(!strcmp(*check, "|")) {
#ifdef DEBUG_MODE
				fprintf(stderr, "Found a pipe character in argv[%ld]\n", check-argv);
#endif

				pipefd = calloc(2, sizeof(int));
				if(pipe(pipefd) == -1) {
					perror("ERROR: pipe failed");
					goto cleanup;
				}

#ifdef DEBUG_MODE
				fprintf(stderr, "-> Freeing up pipe string\n");
#endif
				free(*check);
				*check++ = NULL;
				argv2 = check;
				if(!*argv2){
					fprintf(stderr, "ERROR: bad syntax\n");
					free(pipefd);
					goto cleanup;
				}
			}
			if(!strcmp(*check, "&")) {
				if(check[1] != NULL) {
					fprintf(stderr, "ERROR: bad syntax\n");
					goto cleanup;
				}
				free(check[0]);
				check[0] = NULL;
				background_processing = true;
			}
		}

#ifdef DEBUG_MODE
		fprintf(stderr, "Ready to run: %s", command_line);
#endif
		
		//Execute the command and wait for it to finish
		int failure = 0;
		if(argv2) {
			failure += run_command(argv, 0, pipefd[1], background_processing, pipefd);
			failure += run_command(argv2, pipefd[0], 1, background_processing, pipefd);
			if(background_processing) running_processes += 2 - failure;
		} else {
			failure = run_command(argv, 0, 1, background_processing, pipefd);
			if(background_processing) running_processes += 1 - failure;
		}
		if(failure) exit_code = EXIT_FAILURE;

cleanup:
		//Clear up any running processes
		if(running_processes) {
			int *exit_status = calloc(1, sizeof(int));
			int terminated_pid = waitpid(-1, exit_status, WNOHANG);
			while(terminated_pid){
				if(terminated_pid == -1) {
					perror("ERROR: waitpid failed");
					break;
				}
				printf("[process %d terminated with exit status %d]\n", terminated_pid, WEXITSTATUS(*exit_status));
				if(!--running_processes) break;
				terminated_pid = waitpid(-1, exit_status, WNOHANG);
			}
			free(exit_status);
		}
#ifdef DEBUG_MODE
		fprintf(stderr, "There are still %d running processes\n", running_processes);
#endif
		
		//Free up the argument list
		for(char** entry = argv; *entry; entry++) free(*entry); 
		if(argv2) for(char** entry = argv2; *entry; entry++) free(*entry);
		free(argv);
		free(pipefd);
		
		if(exit_code >= 0) break;
	}

	//Free up everything used
	free(current_directory);
	free(command_line);
	return exit_code;
}
