#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#define MAX_INPUT_SIZE 1024
#define MAX_TOKEN_SIZE 64
#define MAX_NUM_TOKENS 64

/* Splits the string by space and returns the array of tokens
*
*/
char **tokenize(char *line)
{
  char **tokens = (char **)malloc(MAX_NUM_TOKENS * sizeof(char *));
  char *token = (char *)malloc(MAX_TOKEN_SIZE * sizeof(char));
  int i, tokenIndex = 0, tokenNo = 0;

  for(i =0; i < (int)strlen(line); i++){

    char readChar = line[i];

    if (readChar == ' ' || readChar == '\n' || readChar == '\t'){
      token[tokenIndex] = '\0';
      if (tokenIndex != 0){
	tokens[tokenNo] = (char*)malloc(MAX_TOKEN_SIZE*sizeof(char));
	strcpy(tokens[tokenNo++], token);
	tokenIndex = 0;
      }
    } else {
      token[tokenIndex++] = readChar;
    }
  }

  free(token);
  tokens[tokenNo] = NULL ;
  return tokens;
}

/* Handles execution of built-in commands, cd, and exit
*
*/
int execute(char **tokens, int status, int background, int parallel, int *pids) {
  // return 0 if exited
  if (strcmp(tokens[0],"exit") == 0) {
    return 0;
  }

  // cd not built in, so we must handle it separately
  if (strcmp(tokens[0],"cd") == 0) {
    status = chdir(tokens[1]);
    if (status == -1) {
      printf("%s\n",strerror(errno));
    }
  }

  // otherwise, we execute the command
  else {

    pid_t pid;

    //fork child process
    pid = fork();

    if (pid == -1) {
      // -1 means error has occurred
      printf("forking error\n");
    }
    else if (pid == 0) {
      // 0 means child process was created
      // we now exec child
      execvp(tokens[0],tokens);
      printf("Shell: Incorrect command\n");
    }
    else {
      // anything else is a child process being returned to the parent

      // we must remember the child's pid if &&& or & was used
      if (background || parallel) {
        // set the first empty element to be the child's pid
        int i = 0;
        while (pids[i] != 0) {
          i++;
        }
        pids[i] = pid;
      }
      // we set the pgid of all children to be process group 2
      setpgid(pid, 2);

      // if dealing with & case
      if (background) {
        // we must move the process to its own group
        setpgid(pid, 0);

        // we then attempt to reap dead children but do not wait
        pid = waitpid(pid, &status, WNOHANG);
      }
      // if we are dealing with the &&& case
      else if (parallel) {
        // we must temporarily stop the children
        kill(pid, SIGSTOP);
      }
      // otherwise single command or && case
      else {
        // we wait for the child to finish executing
        pid = waitpid(pid, &status, WUNTRACED);

        // if we were stopped by a signal, then we return 2
        if (WIFSIGNALED(status)) {
          return 2;
        }
      }

    }
  }
  // if successful (child finished or running in background), we return 1
  return 1;
}

/* Handles CTRL+C
*
*/
void handler(int sig) {
  (void)sig;
  signal(SIGINT, handler);

  // we want to only kill processes in group 2
  // this way, we do not kill the shell or background processes
  killpg(-2, SIGKILL);

  // just to make formatting look nicer
  printf("\n");
}

int main(int argc, char* argv[]) {
	char  line[MAX_INPUT_SIZE];
	char  **tokens;

  // the most processes we can have is limited by the input size
  int *background_pids = (int *)malloc(MAX_NUM_TOKENS * sizeof(int));

  // we want to make sure all elements start as 0
  memset(background_pids, 0, sizeof(*background_pids));
	int i;

  // this status will be used for waitpid
	int status;

  // open file if specified
	FILE* fp;
	if(argc == 2) {
		fp = fopen(argv[1],"r");
		if(fp < 0) {
			printf("File doesn't exist.");
			return -1;
		}
	}

  // run shell
	while(1) {
		/* BEGIN: TAKING INPUT */
		bzero(line, sizeof(line));
		if(argc == 2) { // batch mode
			if(fgets(line, sizeof(line), fp) == NULL) { // file reading finished
				break;
			}
			line[strlen(line) - 1] = '\0';
		} else { // interactive mode

      // every time we execute, we want to reap dead children that
      // may have finished executing in the background
      for (int pid = 0; background_pids[pid] != 0; pid++) {
        // for each child, we attempt to reap
        if (waitpid(background_pids[pid], &status, WNOHANG) > 0) {
          // if successful, we notify the user
          printf("Shell: Background process finished\n");

          // we then remove the pid of the finished process
          for (int shleft = pid; background_pids[shleft] != 0; shleft++) {
						background_pids[shleft]=background_pids[shleft+1];
					}
        }
      }
			printf("$ ");
			scanf("%[^\n]", line);
			getchar();
		}
		/* END: TAKING INPUT */

		// if empty string, we simply skip
		// other situations like unknown commands will be caught later
		if (strcmp(line,"") == 0) {
			continue;
		}

    // this will allow us to catch SIGINT (CTRL+C)
    signal(SIGINT, handler);

		line[strlen(line)] = '\n'; //terminate with new line

    // used to hold commands for && and &&& cases
    char *temp_cmd;

    // we check to see if &&& is a substring
    if (strstr(line, "&&&") != NULL) {
      // we want to remember all pids of parallel commands
      int *parallel_pids = (int *)malloc(MAX_NUM_TOKENS * sizeof(int));

      // like before, we ensure that all elements are 0
      memset(parallel_pids, 0, sizeof(*parallel_pids));

      // we grab the first command
      temp_cmd = strtok(line, "&&&");

      // loop through commands
      while (temp_cmd != NULL) {
        // we tokenize each command
        tokens = tokenize(temp_cmd);

        // begin executing each command
        if (execute(tokens, status, 0, 1, parallel_pids) == 0) {
          // if command was exit, then we exit
          exit(0);
        }

        // grab the next command
        temp_cmd = strtok(NULL, "&&&");
      }

      // we resume each process that has been paused so that they
      // can run in parallel
      for (int pid = 0; parallel_pids[pid] != 0; pid++) {
          kill(pid, SIGCONT);
      }

      // as long as at least one process is still running, we loop
      while (parallel_pids[0] != 0) {
        // for each iteration, we check on all children
        for (int pid = 0; parallel_pids[pid] != 0; pid++) {
          // attempt to reap dead children
          if (waitpid(parallel_pids[pid], &status, WNOHANG) > 0) {
            // if successful, we remove the child's pid from the array
            for (int shleft = pid; parallel_pids[shleft] != 0; shleft++) {
  						parallel_pids[shleft]=parallel_pids[shleft+1];
  					}
          }
        }
      }
      // lastly, we free up the memory used
      free(parallel_pids);
    }
    // we check to see if && is a substring
    else if (strstr(line, "&&") != NULL) {
      // grab the first command
      temp_cmd = strtok(line, "&&");

      //loop through the commands
      while (temp_cmd != NULL) {
        // tokenize the current command
        tokens = tokenize(temp_cmd);

        // execute the command
        int ret = execute(tokens, status, 0, 0, background_pids);
        if (ret == 0) {
          // if the command was exit, then exit
          exit(0);
        } else if (ret == 2) {
          // special case for parallel, if stopped by a signal,
          // we want to break so that the next command does not execute
          break;
        }
        // grab the next command
        temp_cmd = strtok(NULL, "&&");
      }
    }
    // otherwise, we have a single command (fg or bg)
		else {
      // tokenize the command
      tokens = tokenize(line);

      // we keep track if & case
      int background = 0;

      // check & case
      for (int i = 0; tokens[i] != NULL; i++) {
        if (strcmp(tokens[i],"&") == 0) {
          // if so, change our background flag
          background = 1;

          // remove & from the command, it is no longer needed
          tokens[i] = NULL;
          break;
        }
      }
      // now we execute the command (fg or bg again)
      if (execute(tokens, status, background, 0, background_pids) == 0) {
        // if command was exit, we exit
        exit(0);
      }
    }

		// Freeing the allocated memory
		for(i=0;tokens[i]!=NULL;i++){
			free(tokens[i]);
		}
		free(tokens);


	}

  // freeing allocated memory
  free(background_pids);
	return 0;
}
