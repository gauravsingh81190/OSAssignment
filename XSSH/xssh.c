#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/queue.h>

#define BUFLEN 128
#define INSNUM 13


/**
* @brief Enum describing the state of process
* SHELL will exactly one process for each command. Which can be in 
* process can be in either of four states (STOPPED, RUNNING, TERMINATED, KILLED).
*/
typedef enum _process_state
{
    /*
    * Process will go to stopped states when it recives either (SIGSTOP or SIGTSTP) signal.
    * Usually when we do CTRL+Z shell sends SIGTSTP signal itself and all of its children.
    */
    XSSH_PROC_STATE_STOPPED,

    /*When process is running either in foregorunf or background*/  
    XSSH_PROC_STATE_RUNNING, 

    /*When process has exited*/ 
    XSSH_PROC_STATE_TERMINATED,

    /*When process is killed by any signal could be SIGINT or SIGKILL*/
    XSSH_PROC_STATE_KILLED
}process_state;


/**
* @brief  Enum describing the state of Job.
*     A job is a logical entity representing a single or group of processes. It is describe in detail  struct _job_info
*     description.
*     A Job can be in either of four states (STOPPED, RUNNING, DONE, KILLED). 
*/
typedef enum _job_state
{
    /*When all the process in the job will go to stopped state shell will set the job status to STOPPED*/
    XSSH_JOB_STATE_STOPPED,
    /*If job has atleast one process in running state then job state will be RUNNING*/
    XSSH_JOB_STATE_RUNNING,

    /*If last process of job terminated sucessfully*/
    XSSH_JOB_STATE_DONE,
    /*If last process of job killed*/
    XSSH_JOB_STATE_KILLED
}job_state;


char* state_str[] = 
{
    "STOPPED",
    "RUNNING",
    "DONE",
    "KILLED",
};



/**
* @brief  Struct is being used to store information regarding input/output rediection within a process.
* 
* A process(command) can have more than one I/O redirection .
*
*   Ex:
*          ls -l > /info.txt 2>&1
*          
*          In above command there is two I/O rediection 
*          
*          1)  > /info  (Here output of standard out descriptor '1' is being redirected to file '/info'.
*
*          2)  2>&1   (Here output of standard error descriptor '2' is being redirected to standard out
*                      descriptor '1')/
*
*  XSSH support 5 type of I/O rediection and redirect_info->mode identifies type of redirection info.
*  
*  1) Output redirection from descriptor to file  (eg. > FilePath, 2>FilePath).
*     If no descrptor number is preceding an output redirection then XSSH shell assumes descriptor standard
*     output descriptor (e.g.  1).
*
*  2) Output redirection from descriptor to file in append mode (eg. >> FilePath, 2>> FilePath)
*
*  3) Output redirection from one descriptor to another descriptor (e.g. >&2, 2>&1, 2>&3). 
*
*  4) Input redirection from file to descriptor (e.g. < FilePath, 3< FilePath).
*
*  5) Input redirection from one descriptor to another descriptor (e.g. 1<&3).
*/

typedef struct _redirect_info
{
    /*Src file desciptor number which content is being redirected to another file or descriptor*/
    int srcfd;

    /* Src file which content is being redirected. It is used only in case of input redirection (redirection mode 4)*/
    char *srcfile;

    /*Destination descriptor in which output from another descriptor or input from file is being redirected*/
    int dstfd;
    
    /*Destination file in which output of a descriptor is redirected to*/
    char    *dstfile;
    
    /*Redirection type*/
    int mode;

    
    CIRCLEQ_ENTRY(_redirect_info) link; 
}redirect_info;

/**
* @brief  Struct is being used to store information regarding a process(command).
* XSSH creates a seperate process of each command it is executing.
* 
*/ 
typedef struct _proc_info
{
    /*Process group id of a process*/
    pid_t   pgid;
    
    /*Process id of a process*/
    pid_t   pid;

    /*Command line arguments to command*/
    char   **args;
    int    nargs;

    /*State of process*/
    process_state state;

    /*It is just a flag used in parsing to specify if process is supposed to be run in backgroung
    *
    *  e.g.  find / & (Process for this command will run in background) 
    */
    int    background;

    /*List of redirection info */
    CIRCLEQ_HEAD(ril_head, _redirect_info) redirect_info_list;

    CIRCLEQ_ENTRY(_proc_info) link; 
}proc_info;

/**
* @brief  Struct is being used to store information of a job.
*      A job represents one or more than process grouped which shall be part of same process group.
*      Usually a job contains only one process but it can have more than one process when multiple commads are piped together.
*      So for each command there will be a process.
*      Ex:
*           cat info.txt | wc -l # For this command 
*
*      Above commands are piped together.
*      Hence XSSH allocates a single job_info structure and then add process in it's job_info->proc_info_list in 
*      same order as they are present in command.
*
*/
typedef struct _job_info
{
    /*Process group id of job.
    * 
    * As soon as XSSH spawns the first process it puts that process into a processgroup which id will 
    * be same as process id.
    *
    * Same process group id will be process group id for job as well.
    *
    * After the first process if job has more than one process they will be added to same process group. 
    */
    pid_t pgid;

    /*Process id of last process in the job's processes's list*/
    pid_t lastpid;


    job_state state;

    /*If job is running or supposed to run as a background process*/
    int  background;

    int  job_spec;
    char cmd[BUFLEN];
    
    /*Total number of active process in the job*/
    int  nprocs;    //total number of active processs in the job
    
    /*Total number of stopped processes in the job*/
    int  nstopped;  //number of stopped process within the job

    /*Total number of running processes in the job*/
    int  nrunning;  //number of running process within the job

    /*Status of the last process*/
    int  status;   
    CIRCLEQ_HEAD (pil_head, _proc_info)  proc_info_list; 
    CIRCLEQ_ENTRY(_job_info) link; 
}job_info;

typedef struct _xssh_global_context
{
    CIRCLEQ_HEAD(jobs_head, _job_info) bg_jobs;
    job_info *fg_job;
    int max_bg_job_index;
    int last_bg_job_index;
    int last_status;
    char last_cmd[BUFLEN];
}xssh_global_context;

xssh_global_context g_context;

job_info *create_job(char cmd[BUFLEN]);
void destroy_job(job_info *job); 
proc_info *create_proc(const char *proc_buffer);
void destroy_proc(proc_info *process);
void destroy_redirectinfo(redirect_info *rinfo);

void process_stopped(job_info *job, pid_t pid);
void process_continued(job_info *job, pid_t pid);
void process_terminated(job_info *job, pid_t pid, int status);
void process_killed(job_info *job, pid_t pid, int signal);

void fg_job_continued();
void fg_job_terminated();
void fg_job_stopped();
int execute_job(job_info *job);
void wait_job();
void wait_background_job(int pstatus);
void print_job_status(job_info *job);

void suspend_job(job_info *job);
void resume_job(job_info *job);
void bring_job_to_fg(job_info *job);
void send_job_to_bg(job_info *job, int resume);

void sigint_fg_job();
void sigtstp_fg_job();


/*internal instructions*/
char *instr[INSNUM] = {"show","set","export","unexport","show","exit","wait","help", "bg", "fg", "jobs", "pwd", "cd"};
/*predefined variables*/
/*varvalue[0] stores the rootpid of xssh*/
/*varvalue[3] stores the childpid of the last process that was executed by xssh in the background*/
int varmax = 3;
char varname[BUFLEN][BUFLEN] = {"$\0", "?\0", "!\0",'\0'};
char varvalue[BUFLEN][BUFLEN] = {'\0', '\0', '\0'};

/*remember pid*/
int childnum = 0;
pid_t childpid = 0;
pid_t rootpid = 0;

/*current dir*/
char rootdir[BUFLEN] = "\0";

/*functions for parsing the commands*/
int deinstr(char buffer[BUFLEN]);
void substitute(char *buffer);
void ltrim(char *str);
void rtrim(char *str);

/*functions to be completed*/
int xsshexit(char buffer[BUFLEN]);
void show(char buffer[BUFLEN]);
void help(char buffer[BUFLEN]);
int program(char buffer[BUFLEN]);
void catchctrlc();
void catchctrlz();
void ctrlc_sig(int sig);
void ctrlz_sig(int sig);
void waitchild(char buffer[BUFLEN]);
void set(char buffer[BUFLEN]);
void export(char buffer[BUFLEN]);
void unexport(char buffer[BUFLEN]);
void bg(char buffer[BUFLEN]);
void fg(char buffer[BUFLEN]);
void jobs(char buffer[BUFLEN]);
void pwd();
void cd(char buffer[BUFLEN]);


void run_exec (int inprevpipe, int inpipe, int outpipe, proc_info *p);
/*for optional exercise, implement the function below*/
int pipeprog(char buffer[BUFLEN]);

/*main function*/
int main()
{
    memset(&g_context, 0, sizeof(g_context));
    CIRCLEQ_INIT(&g_context.bg_jobs);
    
    /*set the variable $$*/
    rootpid = getpid();
    childpid = rootpid;
    sprintf(varvalue[0], "%d\0", rootpid);
    /*capture the ctrl+C*/

    catchctrlc();
    catchctrlz();

    /*run the xssh, read the input instrcution*/
    int xsshprint = 0;
    if(isatty(fileno(stdin))) xsshprint = 1;
    if(xsshprint) printf("xssh>> ");
    char buffer[BUFLEN];
    int do_wait = 1;
    while(fgets(buffer, BUFLEN, stdin) > 0)
    {
        /*substitute the variables*/
        substitute(buffer);
        /*delete the comment*/
        char *p = strchr(buffer, '#');
        if(p != NULL)
        {
            *p = '\n';
            *(p+1) = '\0';
        }
        /*decode the instructions*/
        //fprintf(stdout, "buffer=%s", buffer);
        int ins = deinstr(buffer);
        /*run according to the decoding*/
        if(ins == 1)
            show(buffer);
        else if(ins == 2)
            set(buffer);
        else if(ins == 3)
            export(buffer);
        else if(ins == 4)
            unexport(buffer);
        else if(ins == 5) show(buffer); //Not used for now
        else if(ins == 6)
            xsshexit(buffer);
        else if(ins == 7)
            waitchild(buffer);
        else if(ins == 8)
            help(buffer);
        else if(ins == 9)
            bg(buffer);
        else if(ins == 10)
            fg(buffer);
        else if(ins == 11)
        {
            jobs(buffer);
        }
        else if(ins == 12)
            pwd(buffer);
        else if(ins == 13)
            cd(buffer);
        else if(ins == 14)
            time(NULL);    
        else
        {
            //Parsing the Command buffer
            job_info *job = create_job(buffer);
            
            //Executing the job
            int retval = execute_job(job);
            job->state =  XSSH_JOB_STATE_RUNNING;

            if(retval == 0 && job->background)
            {
                send_job_to_bg(job, 0);
                fprintf(stdout, "[%d] %s &\n", job->job_spec, job->cmd);
            }
            else
                g_context.fg_job = job;
        }

        wait_job();

        if(xsshprint) printf("xssh>> ");
        memset(buffer, 0, BUFLEN);
    }
    return -1;
}

/*exit I*/
int xsshexit(char buffer[BUFLEN])
{
    int i, start =4;
    if(buffer[4]!=' '&& buffer[4]!='\0'&& buffer[4]!='\n') //To Handle case where command starts with exit*. eg: "exitsfsfwef"
    {
        printf("-xssh: Unable to execute the instruction %s",buffer);
        return -1;
    }
    //start=5;

    char number[BUFLEN] = {'\0'};
    while(buffer[start]==' ')start++;
    for(i = start; (i < strlen(buffer))&&(buffer[i]!='\n')&&(buffer[i]!='#'); i++)
    {
        number[i-start] = buffer[i];
    }
    number[i-start] = '\0';

    if (strlen(number)==0) exit(0);

    char *endptr;
    int exitval = strtol(number, &endptr, 10);

    if((*number != '\0')&&(*endptr == '\0'))
    {
        exit(exitval);
    }
    else //invalid argument
    {
        exit(-1);
    }

    //FIXME: exit with a return value I that is stored in buffer
    //hint: where is the start of the string of return value I?
    //printf("Replace me with code for exit I\n");

}

/*show W*/
void show(char buffer[BUFLEN])
{
    //FIXME: print the string after "show " in buffer
    //hint: where is the start of this string?
    //printf("Replace me with code for show W\n");
    
    char *ptr = buffer + 5; 
    fprintf(stdout, "%s", ptr);
    sprintf(varvalue[1], "%d", 0);
}

void help(char buffer[BUFLEN])
{
    //FIXME: print the members of your team in the format "Team members: xxx; yyy; zzz" in one line
    //FIXME: print the list of commands that your shell supports

    printf("\n  Team members: Gaurav Singh; Bhavinkumar Parmar; Snehith Chava.");
    printf("\n\n  The following commands are supported.");
    printf("\n  exit I     - Exit the shell and return status I.");
    printf("\n  show W     - Display (print to screen) whatever W is.");
    printf("\n  export W   - set the W as an available variable name.");
    printf("\n  unexport W - remove the existing variable name W.");
    printf("\n  set W1 W2  - set the value of the existing variable W1 as W2.");
    printf("\n  Wait P     - Wait the child process with pid P, and print message.");
    printf("\n  sleep 10&  - Indicating program will be executed in the background.");
    printf("\n  CTRL-C     - Terminate the foreground process but xssh, and print xssh: Exit pid childpid.");
    printf("\n  CTRL-Z     - Suspend and send foreground process to background.");
    printf("\n  comment #  - Blank lines are ignored and support multiple white spaces.");
    printf("\n  show $$    - This will print the pid of the current xssh process.");
    printf("\n  show $!    - This will print the pid of the last process that was executed by xssh in the background.");
    printf("\n  jobs       - List down all the job in backgrouds with their state and backgroud job number.");
    printf("\n  fg         - This command will bring specified or last background(if no argument provided) to foreground.");
    printf("\n\n\tExample : \n\n\t\t 1) fg   #This will resume the last suspended background job and bring it to foreground.");
    printf("\n\t\t 2) fg job_num  #This will resume the specified suspended job and bring that job to foreground.");
    printf("\n  bg         - This command will put the last (if no argument provided) or specified suspended job(by CTRL +Z) to background.");
    printf("\n\n\tExample : \n\n\t\t 1) bg   #This will resume the last suspended background job.");
    printf("\n\t\t 2) bg job_num #This will resume the specified suspended background job to running.");
    printf("\n  cd         - Change the current working ddirectory of SHELL.");
    printf("\n  pwd        - Print the current working directory.");
    printf("\n  Finished optional (a); Finished optional (b).\n\n");
}

void bg(char buffer[BUFLEN])
{
    ltrim(buffer);
    rtrim(buffer);
    
    int found = 0;
    int job_spec = 0;
    if(strlen(buffer + 2) == 0)
        job_spec = g_context.last_bg_job_index;
    else 
        job_spec = strtol(buffer + 2, NULL, 10);

    job_info *job = NULL;
    CIRCLEQ_FOREACH(job, &g_context.bg_jobs, link)
    {
        if(job->job_spec == job_spec)
        {
            CIRCLEQ_REMOVE(&g_context.bg_jobs, job, link);
            found = 1;
            break;
        }
    }

    if(!found)
    {
        if(job_spec == g_context.last_bg_job_index)
            fprintf(stderr, "-xssh: bg: current: no such job\n");
        else
            fprintf(stderr, "-xssh: bg: %s : no such job\n", buffer + 2);
       
        sprintf(varvalue[1], "%d", 1);
        return;
    }

    send_job_to_bg(job, 1);
    fprintf(stdout, "[%d] %s &\n", job->job_spec, job->cmd);
    sprintf(varvalue[1], "%d", 0);
}

void fg(char buffer[BUFLEN])
{
    ltrim(buffer);
    rtrim(buffer);
    
    int found = 0;
    int job_spec = 0;
    if(strlen(buffer + 2) == 0)
        job_spec = g_context.last_bg_job_index;
    else 
        job_spec = strtol(buffer + 2, NULL, 10);


    job_info *job = NULL;
    CIRCLEQ_FOREACH(job, &g_context.bg_jobs, link)
    {
        if(job->job_spec == job_spec)
        {
            CIRCLEQ_REMOVE(&g_context.bg_jobs, job, link);
            found = 1;
            break;
        }
    }

    if(!found)
    {
        if(job_spec == g_context.last_bg_job_index)
            fprintf(stderr, "-xssh: fg: current: no such job\n");
        else
            fprintf(stderr, "-xssh: fg: %s : no such job\n", buffer + 2);
       
        sprintf(varvalue[1], "%d", 1);
        return;
    }

    bring_job_to_fg(job);
    fprintf(stdout, "%s\n", job->cmd);
    sprintf(varvalue[1], "%d", 0);
}

void jobs(char buffer[BUFLEN])
{
    wait_background_job(1);

    /*if(!CIRCLEQ_EMPTY(&g_context.bg_jobs))
    {
        job_info *job = NULL;
        CIRCLEQ_FOREACH(job, &g_context.bg_jobs, link)
            fprintf(stdout, "[%d] %s %s\n", job->job_spec, state_str[job->state], job->cmd);
    }*/         
}

void cd(char buffer[BUFLEN])
{
    int start = 3;
    int count = start;
    int retval = 0;
    
    rtrim(buffer);
    ltrim(buffer + count);
    
    retval = chdir(&buffer[count]);
    if(retval != 0)
    {
        fprintf(stderr, "-xssh: cd: %s: %s\n", &buffer[count], strerror(errno));
        sprintf(varvalue[1], "%d", 1);
        return;
    }

    sprintf(varvalue[1], "%d", 0);
}

void pwd()
{
    printf("%s\n", getcwd(NULL, 0));
    sprintf(varvalue[1], "%d", 1); 
}

/*export variable --- set the variable name in the varname list*/
void export(char buffer[BUFLEN])
{
    int i, j;
    //flag == 1, if variable name exists in the varname list
    int flag = 0;
    //parse and store the variable name in buffer[]
    char str[BUFLEN];
    int start = 7;
    while(buffer[start]==' ')start++;
    for(i = start; (i < strlen(buffer))&&(buffer[i]!='#')&&(buffer[i]!=' ')&&(buffer[i]!='\n'); i++)
    {
        str[i-start] = buffer[i];
    }
    str[i-start] = '\0';
    //hint: try to print "str" and "varname[j]" to see what's stored there
    for(j = 0; j < varmax; j++)
    {
        //FIXME: if the variable name (in "str") exist in the
        //varname list (in "varname[j]"), set the flag to be 1
        //using strcmp()
        
        if(!strcmp(varname[j], str))
        {
            flag = 1;
            break;
        }
    }
    if(flag == 0) //variable name does not exist in the varname list
    {
        //FIXME: copy the variable name to "varname[varmax]" using strcpy()
        //FIXME: set the corresponding value in "varvalue[varmax]" to empty string '\0'
        //FIXME: update the 'varmax' (by +1)
        //FIXME: print "-xssh: Export variable str.", where str is newly exported variable name
      
        strcpy(varname[varmax], str);     
        varvalue[varmax++][0]='\0';
        printf("-xssh: Export variable %s.\n", str);
        sprintf(varvalue[1], "%d", 0);
    }
    else //variable name already exists in the varname list
    {
        //FIXME: print "-xssh: Existing variable str is value.", where str is newly exported variable name and value is its corresponding value (stored in varvalue list)
        printf("-xssh:Existing variable %s is %s.\n", str, varvalue[j]);
        sprintf(varvalue[1], "%d", EEXIST);
    }
}

/*unexport the variable --- remove the variable name in the varname list*/
void unexport(char buffer[BUFLEN])
{
    int i, j;
    //flag == 1, if variable name exists in the varname list
    int flag = 0;
    //parse and store the variable name in buffer[]
    char str[BUFLEN];
    int start = 9;
    while(buffer[start]==' ')start++;
    for(i = start; (i < strlen(buffer))&&(buffer[i]!='#')&&(buffer[i]!=' ')&&(buffer[i]!='\n'); i++)
    {
        str[i-start] = buffer[i];
    }
    str[i-start] = '\0';
    for(j = 0; j < varmax; j++)
    {
        //FIXME: if the variable name (in "str") exist in the
        //varname list (in "varname[j]"), set the flag to be 1
        //using strcmp() --- same with export()
        if(!strcmp(varname[j], str))
        {
            flag = 1;
            break;
        }
    }
    if(flag == 0) //variable name does not exist in the varname list
    {
        //FIXME: print "-xssh: Variable str does not exist.",
        //where str is the variable name to be unexported
        printf("-xssh: Variable %s does not exist.\n", str);
        sprintf(varvalue[1], "%d", ENOENT);
    }
    else //variable name already exists in the varname list
    {
        //FIXME: clear the found variable by setting its
        //"varname" and "varvalue" both to '\0'
        //FIXME: print "-xssh: Variable str is unexported.",
        //where str is the variable name to be unexported
        varname[j][0]='\0';
        varvalue[j][0]='\0';
        printf("-xssh: Variable %s is unexported.\n", str);
        sprintf(varvalue[1], "%d", 0);
    }
}

/*set the variable --- set the variable value for the given variable name*/
void set(char buffer[BUFLEN])
{
    int i, j;
    //flag == 1, if variable name exists in the varname list
    int flag = 0;
    //parse and store the variable name in buffer[]
    char str[BUFLEN];
    int start = 4;

    rtrim(buffer);
    while(buffer[start]==' ')start++;
    for(i = start; (i < strlen(buffer))&&(buffer[i]!=' ')&&(buffer[i]!='#'); i++)
    {
        str[i-start] = buffer[i];
    }
    str[i-start] = '\0';
    while(buffer[i]==' ')i++;
    if(buffer[i]=='\n' || buffer[i] == '\0')
    {
        printf("No value to set!\n");
        sprintf(varvalue[1], "%d", EINVAL);
        return;
    }
    for(j = 0; j < varmax; j++)
    {
        //FIXME: if the variable name (in "str") exist in the
        //varname list (in "varname[j]"), set the flag to be 1
        //using strcmp() --- same with export()
        //
        if(!strcmp(varname[j], str))
        {
            flag = 1;
            break;
        }
    }
    if(flag == 0)
    {
        //FIXME: print "-xssh: Variable str does not exist.",
        //where str is the variable name to be unexported
        printf("-xssh: Variable %s does not exist.\n", str);
        sprintf(varvalue[1], "%d", 2);
    }
    else
    {
        //hint: try to print "buffer[i]" to see what's stored there
        //hint: may need to add '\0' by the end of a string
        //FIXME: set the corresponding varvalue to be value (in buffer[i]) using strcpy()
        //FIXME: print "-xssh: Set existing variable str to value.", where str is newly exported variable name and value is its corresponding value (stored in varvalue list)
        rtrim(buffer);
        start = 4;
        start += strlen(str) + 1;
        ltrim(buffer + start);
        int temp = start;
        while(buffer[start] && !isspace(buffer[start])) start++;
        if(isspace(buffer[start]))
            buffer[start]='\0';

        sprintf(varvalue[j], "%s", buffer + temp);
        printf("-xssh: Set existing variable %s to %s.\n", varname[j], varvalue[j]);
        sprintf(varvalue[1], "%d", 0);
    }
}

/*catch the ctrl+C*/
void catchctrlc()
{
    //FIXME: catch the ctrl+C signal
    //printf("Replace me for catching ctrl+C\n");
    signal(SIGINT, ctrlc_sig);
}

/*catch the ctrl+Z*/
void catchctrlz()
{
    //FIXME: catch the ctrl+C signal
    //printf("Replace me for catching ctrl+C\n");
    signal(SIGTSTP, ctrlz_sig);
}

/*ctrl+C handler*/
void ctrlc_sig(int sig)
{
    if(g_context.fg_job)
        sigint_fg_job();        
    else
    {
        printf("\nxssh>> ");
        fflush(stdout);       
    }
}

void ctrlz_sig(int sig)
{
    if(g_context.fg_job)
        sigtstp_fg_job();
}

/*wait instruction*/
void waitchild(char buffer[BUFLEN])
{
    int i;
    int start = 5;

    /*store the childpid in pid*/
    char number[BUFLEN] = {'\0'};
    while(buffer[start]==' ')start++;
    for(i = start; (i < strlen(buffer))&&(buffer[i]!='\n')&&(buffer[i]!='#'); i++)
    {
        number[i-start] = buffer[i];
    }
    number[i-start] = '\0';
    char *endptr;
    int pid = strtol(number, &endptr, 10);

    /*simple check to see if the input is valid or not*/
    if((*number != '\0')&&(*endptr == '\0'))
    {
        //FIXME: if pid is not -1, try to wait the background process pid
        //FIXME: if successful, print "-xssh: Have finished waiting process pid", where pid is the pid of the background process
        //FIXME: if not successful, print "-xssh: Unsuccessfully wait the background process pid", where pid is the pid of the background process


        //FIXME: if pid is -1, print "-xssh: wait childnum background processes" where childnum stores the number of background processes, and wait all the background processes
        //hint: remember to set the childnum correctly after waiting!
        
        fprintf(stderr, "Waiting for child %d\n", pid);  

        //exit(-1);
        siginfo_t info; 
        int exitstatus = 0;
        int ni = 0;
        do
        { 
            int retval = waitid(pid < 0 ? P_ALL : P_PID, pid, &info, WEXITED);
            if(retval < 0 && errno == ECHILD)
            {
                if(ni > 0) 
                    fprintf(stdout, "-xssh:all child processes are terminated\n");
                else
                    fprintf(stdout, "-xssh:no child process exist\n");

                sprintf(varvalue[1], "%d", 0);
                break;    
            }
            else if(retval < 0)
            {
                if(pid < 0)
                    fprintf(stdout, "-xssh:failed to wait for all child process\n");
                else
                    fprintf(stdout, "-xssh:failed to wait for %d child process\n", pid);
                sprintf(varvalue[1], "%d", errno);
                break;
            }
        
            ni++;    
            if(info.si_code == CLD_EXITED)
            {
                fprintf(stdout, "-xssh: child process %d is terminated with status=%d\n", info.si_pid, info.si_status);
                fflush(stdout);
            }
            else
            {
                fprintf(stdout, "-xssh: child process %d is killed by signal=%d\n", info.si_pid, info.si_status);
                fflush(stdout);
            }


            job_info *job = NULL;
            proc_info *p = NULL;
            int found = 0;
            CIRCLEQ_FOREACH(job, &g_context.bg_jobs, link)
            {
                CIRCLEQ_FOREACH(p, &job->proc_info_list, link)
                    if(info.si_pid == p->pid)
                    { 
                        found = 1;
                        break;
                    }

                if(found)
                    break; 
            }            
            if(found)
            {
                if(info.si_code == CLD_EXITED)
                    process_terminated(job, p->pid, info.si_status);                    
                else
                    process_killed(job, p->pid, info.si_status);                    
                
                CIRCLEQ_REMOVE(&g_context.bg_jobs, job, link);
            }
        }while(pid < 0);
    }
    else
    { 
        printf("-xssh: wait: Invalid pid\n");
        sprintf(varvalue[1], "%d", ((unsigned char)-1));
    }
}

/*execute the external command*/
int program(char buffer[BUFLEN])
{
    /*if backflag == 0, xssh need to wait for the external command to complete*/
    /*if backflag == 1, xssh need to execute the external command in the background*/
    int backflag = 0;
    char *ptr = strchr(buffer, '&');
    if(ptr != NULL) backflag = 1;

    pid_t pid;
    //FIXME: create a new process for executing the external command

    //FIXME: remember to check if the process creation is successful or not. if not, print error message and return -2, see codes below;


    //FIXME: write the code to execute the external command in the newly created process, using execvp()
    //hint: the external command is stored in buffer, but before execute it you may need to do some basic validation check or minor changes, depending on how you execute
    //FIXME: remember to check if the external command is executed successfully; if not, print error message "-xssh: Unable to execute the instruction buffer", where buffer is replaced with the actual external command to be printed
    //hint: after executing the extenal command using execvp(), you need to return -1;
    /*for optional exercise, implement stdin/stdout redirection in here*/

    printf("Replace me for executing external commands\n");

    //FIXME: in the xssh process, remember to act differently, based on whether backflag is 0 or 1
    //hint: the codes below are necessary to support command "wait -1", but you need to put them in the correct place
    childnum++;
    childpid = pid;
    childnum--; //this may or may not be needed, depending on where you put the previous line
    //hint: the code below is necessary to support command "show $!", but you need to put it in the correct place
    sprintf(varvalue[2], "%d\0", pid);
    return 0;
}

/*for optional exercise, implement the function below*/
/*execute the pipe programs*/
int pipeprog(char buffer[BUFLEN])
{
    printf("-xssh: For optional exercise: currently not supported.\n");
    return 0;
}

/*substitute the variable with its value*/
void substitute(char *buffer)
{
    char newbuf[BUFLEN] = {'\0'};
    int i;
    int pos = 0;
    for(i = 0; i < strlen(buffer);i++)
    {
        if(buffer[i]=='#')
        {
            newbuf[pos]='\n';
            pos++;
            break;
        }
        else if(buffer[i]=='$')
        {
            if((buffer[i+1]!='#')&&(buffer[i+1]!=' ')&&(buffer[i+1]!='\n'))
            {
                i++;
                int count = 0;
                char tmp[BUFLEN];
                for(; (buffer[i]!='#')&&(buffer[i]!='\n')&&(buffer[i]!=' '); i++)
                {
                    tmp[count] = buffer[i];
                    count++;
                }
                tmp[count] = '\0';
                int flag = 0;
                int j;
                for(j = 0; j < varmax; j++)
                {
                    if(strcmp(tmp,varname[j]) == 0)
                    {
                        flag = 1;
                        break;
                    }
                }
                if(flag == 0)
                {
                    printf("-xssh: Does not exist variable $%s.\n", tmp);
                }
                else
                {
                    strcat(&newbuf[pos], varvalue[j]);
                    pos = strlen(newbuf);
                }
                i--;
            }
            else
            {
                newbuf[pos] = buffer[i];
                pos++;
            }
        }
        else
        {
            newbuf[pos] = buffer[i];
            pos++;
        }
    }
    if(newbuf[pos-1]!='\n')
    {
        newbuf[pos]='\n';
        pos++;
    }
    newbuf[pos] = '\0';
    strcpy(buffer, newbuf);
    //printf("Decode: %s", buffer);
}

void ltrim(char *str)
{
    char *ptr = str;
    while(isspace(*ptr) || *ptr == '\n')
        ptr++;
    memmove(str, ptr, strlen(ptr) + 1);
}

void rtrim(char *str)
{
    char *ptr = str + strlen(str) - 1;
    while(ptr >= str && (isspace(*ptr) || *ptr == '\n'))
        *(ptr--)='\0';
}

int isampersand(char c)
{
    return c == '&';   
}

int isinredir(char c)
{
    return c == '<';
}

int isoutredir(char c)
{
    return c == '>';
}

int isredir(char c)
{
    return (isinredir(c) || isoutredir(c));
}

int isvalidtokenchar(char c)
{
    return !(isinredir(c) || isoutredir(c) || isampersand(c) || isspace(c) || c == '\0');
}

int isvalidfd(const char *ptr)
{
    int start=0;
    if(strlen(ptr) == 0)
        return 0;

    while(ptr[start] && isdigit(ptr[start])) start++;
    if(!ptr[start])
        return 1;

    return 0;
}



/*decode the instruction*/
int deinstr(char buffer[BUFLEN])
{
    int i;
    int flag = 0;
    for(i = 0; i < INSNUM; i++)
    {
        flag = 0;
        int j = 0;
        int stdlen = strlen(instr[i]);
        ltrim(buffer);
        int len = strlen(buffer);
        if((len == 0) ||(buffer[0]=='#'))
        {
            flag = 0;
            i = INSNUM;
            break;
        }
        for(j = 0; (j < len)&&(j < stdlen); j++)
        {
            if(instr[i][j] != buffer[j])
            {
                flag = 1;
                break;
            }
        }
        
        if((flag == 0) && (i >= 8) && (i <=12))
        {
            break;
        }
        else if((flag == 0) && (j  == stdlen) && (j <= len) && (buffer[j] == ' '))
        {
            break;
        }
        else if((flag == 0) && (j == stdlen) && (j <= len) && (i == 5))
        {
            break;
        }
        else if((flag == 0) && (j == stdlen) && (j <= len) && (i == 7))
        {
            break;
        }
        else
        {
            flag = 1;
        }
    }
    if(flag == 1)
    {
        i = 0;
    }
    else
    {
        i++;
    }
    return i;
}

/**
* @brief This function parse the Command buffer and determines information such as command arguments and recirecton info.
*
* @param proc_buffer [IN] Command buffer for a single command being parsed.
*
* @return instance of proc_info structure on sucess else NULL
*/
proc_info * create_proc(const char *proc_buffer)
{
    int retval = 0;
    int token = 0;
    int len = 0;
    int i = 0;
    int j = 0;
    int nargs = 0; 
    char *cur_token = NULL;
    char *prev_token = NULL;
    char *cmdbuffer = NULL;
    char *endcmdbuffer = NULL;

    proc_info *p = NULL;
    redirect_info *rinfo = NULL;

    len = strlen(proc_buffer);
    cur_token = malloc(BUFLEN);
    if(!cur_token)
    {
        fprintf(stderr, "-xssh:%s(%d) malloc failed", __FUNCTION__, __LINE__);
        retval = -1;
        goto done;
    }
    memset(cur_token, 0, BUFLEN);

    prev_token = malloc(BUFLEN);
    if(!prev_token)
    {
        fprintf(stderr, "-xssh:%s(%d) malloc failed", __FUNCTION__, __LINE__);
        retval = -1;
        goto done;
    }
    memset(prev_token, 0, BUFLEN);

    cmdbuffer = malloc(BUFLEN);
    if(!cmdbuffer)
    {
        fprintf(stderr, "-xssh:%s(%d) malloc failed", __FUNCTION__, __LINE__);
        retval = -1;
        goto done;
    }
    memset(cmdbuffer, 0, BUFLEN);

    p = malloc(sizeof(proc_info));
    if(!p)
    {
        fprintf(stderr, "-xssh:%s(%d) malloc failed", __FUNCTION__, __LINE__);
        retval = -1;
        goto done;   
    }
    memset(p, 0, sizeof(proc_info));
    CIRCLEQ_INIT(&p->redirect_info_list); 

    while(i <= len)
    {
        // if character is one of '>', '<', '&', '\0', ' ' then is invalid char for a token. 
        if(!isvalidtokenchar(proc_buffer[i]))
        {
            if(token)
            { 
                memcpy(cur_token, &proc_buffer[j], i - j);
                cur_token[i - j] = '\0';
                token = 0;
            }
        }
        else
        {
            if(!token)
            {
                j = i;
                token = 1;
            }
            i++;
            continue;
        } 

        if(rinfo)
        {
            if(strlen(cur_token))
            { 
                int fd = -1;
                char *file = strdup(cur_token); 

                if(file == NULL)
                {
                    fprintf(stderr, "-xssh:%s(%d)]: malloc failed", __FUNCTION__, __LINE__);
                    retval = -1;
                    goto done;
                }

                if(isvalidfd(cur_token))
                    fd = atol(cur_token);

                if(rinfo->mode == 1)
                {
                    rinfo->dstfile = file;
                    rinfo->dstfd = -1;
                    //fprintf(stdout, "%d>%s\n", rinfo.srcfd, rinfo.dstfile);
                }

                if(rinfo->mode == 2)
                {
                    rinfo->dstfile = file;
                    rinfo->dstfd = -1;
                    //fprintf(stdout, "%d>>%s\n", rinfo.srcfd, rinfo.dstfile);
                }

                if(rinfo->mode == 3)
                {
                    rinfo->dstfd = fd;
                    rinfo->dstfile = file;

                    if(rinfo->dstfd >=0)
                    {
                        free(rinfo->dstfile);
                        rinfo->dstfile = NULL;
                    }

                    if(rinfo->dstfile)
                    {
                        if(rinfo->srcfd != 1)
                        {
                            fprintf(stderr, "-xssh:%s ambiguous redirect", cur_token);
                            retval = -1;
                            goto done;
                        }
                        else
                            rinfo->mode = 1;
                    }

                    //if(rinfo->dstfile)
                    //fprintf(stdout, "%d>%s\n", rinfo.srcfd, rinfo.dstfile);
                    //else
                    //fprintf(stdout, "%d>&%d\n", rinfo.srcfd, rinfo.dstfd);
                }

                if(rinfo->mode == 4)
                {
                    rinfo->srcfd = -1;
                    rinfo->srcfile = file;
                    //fprintf(stdout, "%d<%s\n", rinfo->dstfd, rinfo->srcfile);   
                }

                if(rinfo->mode == 5)
                {
                    rinfo->dstfd = fd;
                    rinfo->dstfile = file;

                    if(rinfo->dstfd >=0)
                    {
                        free(rinfo->dstfile);
                        rinfo->dstfile = NULL;
                    }

                    if(rinfo->dstfile)
                    {
                        fprintf(stderr, "-xssh:%s ambiguous redirect", cur_token);
                        retval = -1;
                        goto done;
                    }

                    //fprintf(stdout, "%d<&%d\n", rinfo->dstfd, rinfo->srcfd);   
                }

                CIRCLEQ_INSERT_TAIL(&p->redirect_info_list, rinfo, link);
                rinfo = NULL;
                cur_token[0] = '\0';
            }
        }

        if(p->background)
        {
            if(strlen(cur_token) != 0)
            {
                fprintf(stderr, "-xssh:%s ambiguous redirect", cur_token);
                retval = -1;
                goto done;
            }
        }

        //symbol 
        memcpy(prev_token, cur_token, strlen(cur_token) + 1);
        cur_token[0] = '\0';

        //if char is either '<' or '>'.
        if(isredir(proc_buffer[i]))
        {
            if(rinfo || p->background)
            {
                fprintf(stderr, "-xssh: syntax error near unexpected token `%c'", proc_buffer[i]);
                retval = -1;
                goto done;
            }

            rinfo = malloc(sizeof(redirect_info));
            if(!rinfo)
            {
                fprintf(stderr, "-xssh:%s(%d) malloc failed", __FUNCTION__, __LINE__);
                retval = -1;
                goto done;
            }
            memset(rinfo, 0, sizeof(redirect_info)); 
            rinfo->srcfd = -1;
            rinfo->srcfile = NULL;
            rinfo->dstfile = NULL;
            rinfo->dstfd = -1; 

            int fd = -1;
            int start = 0;

            if(isvalidfd(prev_token))
                fd = atol(prev_token);

            if(fd >= 0)
                prev_token[0]='\0';

            if(isoutredir(proc_buffer[i]))
            {
                rinfo->srcfile = NULL; 
                rinfo->srcfd = fd; 
                if(rinfo->srcfd < 0)
                    rinfo->srcfd = 1;

                rinfo->mode = 1;
                if(((i + 1) < len) && isoutredir(proc_buffer[i + 1]))
                {
                    rinfo->mode = 2;
                    i++;
                }
                else if(((i + 1)< len) && isampersand(proc_buffer[i + 1]))
                {
                    rinfo->mode = 3;
                    i++;
                }
            }
            else
            {
                rinfo->srcfd = -1;
                rinfo->srcfile = NULL;
                rinfo->dstfile = NULL;
                rinfo->dstfd = fd; 
                if(rinfo->dstfd < 0)
                    rinfo->dstfd = 0;

                rinfo->mode = 4;
                if(((i + 1) < len) && isampersand(proc_buffer[i + 1]))
                {
                    rinfo->mode = 5;
                    i++;
                }
            }

        }
        else if (isampersand(proc_buffer[i]))  //If char is '&'
        {
            if(rinfo)
            {
                fprintf(stderr, "-xssh: syntax error near unexpected token `&'");
                retval = -1;
                goto done;
            }

            p->background = 1;
        }else if(proc_buffer[i] == '\0')
        {
            if(rinfo)
            {
                fprintf(stderr, "-xssh: syntax error near unexpected token `newline'");
                retval = -1;
                goto done;
            }
        }

        if(strlen(prev_token) != 0)
        {
            //consuming it as an argument to program
            //printf("argument = %s\n", prev_token); 
            int len = 0;
            if(!endcmdbuffer)
            {
                endcmdbuffer = cmdbuffer;
                endcmdbuffer += sprintf(endcmdbuffer, "%s", prev_token);   
            }
            else
                endcmdbuffer += sprintf(endcmdbuffer, " %s", prev_token);

            nargs++;
        }
        i++; 
    }

    if(nargs)
    {
        int cnt = 0;
        char *ptr = NULL;
        char *saveptr = NULL;

        nargs +=1; 
        p->args = malloc(sizeof(char*) * (nargs + 1));
        if(!p->args)
        {
            fprintf(stderr, "-xssh:%s(%d) malloc failed", __FUNCTION__, __LINE__);
            retval = -1;
            goto done;
        }
        memset(p->args, 0, p->nargs);
        p->nargs = nargs;

        ptr = strtok_r(cmdbuffer, " ", &saveptr);

        p->args[cnt] = strdup(ptr);
        if(!p->args[cnt])
        {
            fprintf(stderr, "-xssh:%s(%d) malloc failed", __FUNCTION__, __LINE__);
            retval = -1;
            goto done;
        }
        cnt++; 
        while(ptr != NULL)
        { 
            p->args[cnt] = strdup(ptr);
            if(!p->args[cnt])
            {
                fprintf(stderr, "-xssh:%s(%d) malloc failed", __FUNCTION__, __LINE__);
                retval = -1;
                goto done;
            }

            ptr = strtok_r(NULL, " ", &saveptr);
            cnt++;     
        };

        p->args[cnt] = NULL;
    } 

done:
    if(rinfo)
        destroy_redirectinfo(rinfo);

    if(cmdbuffer)
        free(cmdbuffer);

    if(prev_token)
        free(prev_token);

    if(cur_token)
        free(cur_token);

    if(retval != 0)
    {
        if(p)
            destroy_proc(p);
        p = NULL;
    }
    return p;
}

void destroy_redirectinfo(redirect_info *rinfo)
{
    if(!rinfo)
        return;

    if(rinfo->srcfile)
        free(rinfo->srcfile);

    if(rinfo->dstfile)
        free(rinfo->dstfile);

    free(rinfo);
}

void destroy_proc(proc_info *p)
{
    if(!p)
        return;

    while(!CIRCLEQ_EMPTY(&p->redirect_info_list))
    {
        redirect_info *rinfo = CIRCLEQ_FIRST(&p->redirect_info_list); 
        CIRCLEQ_REMOVE(&p->redirect_info_list, rinfo, link);
        destroy_redirectinfo(rinfo);
    }

    if(p->nargs)
    {
        int i = 0;
        for(i = 0; i < p->nargs; i++)
        {
            if(p->args[i])
                free(p->args[i]);
        }
        free(p->args);
    }

    free(p);
}

/**
* @brief  This function parse the command buffer and deteremines processes and thier redirection info then if parsing is successful
* it allocates a job_info structure.
*
* @param buffer[BUFLEN]  [IN] Buffer to parsed
*
* @return job_info structure
*/
job_info * create_job(char buffer[BUFLEN])
{
    int retval = 0;
    int c = 0, k = 0;
    char *saveptr = NULL;
    char *token = NULL;
    char cmdBuffer[BUFLEN]={0};
    job_info *job = NULL;
    proc_info *p = NULL;

    ltrim(buffer);
    rtrim(buffer);

    strcpy(cmdBuffer, buffer);
 

    token = strtok_r(buffer, "|", &saveptr);
    while (token != NULL)
    {
        p = create_proc(token);
        if(!p)
        {
            retval = -1;
            goto done;
        }

        if(!job)
        {
            job = malloc(sizeof(job_info));
            if(!job)
            {
                fprintf(stderr, "-xssh:%s(%d)]: malloc failed", __FUNCTION__, __LINE__);
                retval = -1;
                goto done;
            }
            memset(job, 0, sizeof(job_info));
            CIRCLEQ_INIT(&job->proc_info_list);
        }


       /* If '|' exists. It means more than one commands are piped together. 
        * So each process get appended in job_info's process list in same order 
        * as they are present in command buffer.
        */
        token = strtok_r(NULL, "|", &saveptr);
      
        /*If sets of commands supposed to run in background then last process background field shall be set
         * to 1 by create_proc functon.
         * If background flag is set for a command but it is not the last command in command list
         * then parser will fail.
         */
        if(p->background && token)
        {
            fprintf(stderr, "-xssh: syntax error near unexpected token `|'");
            retval = -1;
            goto done;
        }
        
        if(p->background)
            job->background = 1;   

        CIRCLEQ_INSERT_TAIL(&job->proc_info_list, p, link);
        job->nprocs++;
        p = NULL; 
    }


    if(job->background)
        cmdBuffer[strlen(cmdBuffer)-1]='\0';
    strcpy(job->cmd, cmdBuffer);

done:

    if(p)
        destroy_proc(p);
    if(retval != 0)
    {
        destroy_job(job);
        job = NULL;
    }
    return job;
}

void destroy_job(job_info *job)
{
    if(!job)
        return;

    while(!CIRCLEQ_EMPTY(&job->proc_info_list))
    {
        proc_info *p = CIRCLEQ_FIRST(&job->proc_info_list);
        CIRCLEQ_REMOVE(&job->proc_info_list, p, link);
        destroy_proc(p);
    }

    free(job);
}

/**
* @brief  This function will execute the command using execvp and shall be called just after fork() in execute_job function.
* but before it does execvp it also does some prequired task such pipe and redirection setup
*
* @param inprevpipe  previous pipe's in open file discritor 
* @param inpipe      current pipe in open file descriptor.
* @param outpipe     current pipe out open file descriptor
* @param p
*/
void run_exec (int inprevpipe, int inpipe, int outpipe, proc_info *p)
{
    int retval = 0;
    if(inpipe != 0)    //last process in job will have inpipe as 0 and outpipe as 1
        close(inpipe); 

    if(outpipe != 1)
    {
 
        retval = dup2(outpipe, 1);
        if(retval < 0)
        {
            fprintf(stderr, "-xssh:%s(%d) dup failed\n", __FUNCTION__, __LINE__);
            goto done;
        }

        close(outpipe);    //should we do it  
    }

    if(inprevpipe != 0)
    {
        retval = dup2(inprevpipe, 0);
        if(retval < 0)
        {
            fprintf(stderr, "-xssh:%s(%d) dup failed\n", __FUNCTION__, __LINE__);
            goto done;
        }

        close(inprevpipe); //should we do it 
    }

    //redirection setup

    redirect_info *rinfo = NULL;
    CIRCLEQ_FOREACH(rinfo,  &p->redirect_info_list, link)
    {
        int fd1;
        int fd2;

        if(rinfo->mode == 1)
        {
            int srcfd = rinfo->srcfd;
            char *dstfile = rinfo->dstfile;
            
            fd1 = srcfd; 
            fd2 = open(rinfo->dstfile, O_CREAT | O_WRONLY | O_TRUNC, 0777);
            if(fd2 < 1)
            {
                retval = -errno;
                fprintf(stderr, "-xssh:%s(%d) failed to open file(%s)\n", __FUNCTION__, __LINE__, rinfo->dstfile);
            }
 
            //fprintf(stderr, "file=%s, fd=%d", dstfile, fd2);
        }

        if(rinfo->mode == 2)
        {
            int srcfd = rinfo->srcfd;
            char *dstfile = rinfo->dstfile;
            
            fd1 = srcfd; 
            fd2 = open(dstfile, O_CREAT | O_WRONLY | O_APPEND, 0777);
            if(fd2 < 1)
            {
                retval = -errno;
                fprintf(stderr, "-xssh:%s(%d) failed to open file(%s)\n", __FUNCTION__, __LINE__, rinfo->dstfile);
            } 

        }

        if(rinfo->mode == 3)
        {
            int srcfd = rinfo->srcfd;
            int dstfd = rinfo->dstfd;
            fd1 = srcfd;
            fd2 = dstfd;
        } 

        if(rinfo->mode == 4)
        {
            char *srcfile = rinfo->srcfile;
            int dstfd = rinfo->dstfd;
        
            fd1 = dstfd;
            fd2 = open(srcfile, O_RDONLY);
            if(fd2 < 1)
            {
                retval = -errno;
                fprintf(stderr, "-xssh:%s(%d) failed to open file(%s)\n", __FUNCTION__, __LINE__, rinfo->dstfile);
            } 
        }

        if(rinfo->mode == 5)
        {
            int srcfd = rinfo->srcfd;
            int dstfd = rinfo->dstfd;
            fd1 = dstfd;
            fd2 = srcfd;
        }

        if(dup2(fd2, fd1) < 0)
        {
            retval = -errno;
            fprintf(stderr, "-xssh:%s(%d) failed to dup\n", __FUNCTION__, __LINE__);
            goto done;         
        }
        close(fd2); 
    }

    if(p->nargs > 0)
    {
        retval = execvp(p->args[0], &p->args[1]);
        if(retval < 0)
        {
            fprintf(stderr, "-xssh:%s(%d) execvp failed\n", __FUNCTION__, __LINE__);
            goto done;
        }
    }
    else
        exit(0);
done:
    if(retval < 0)
        exit(-errno);   
}

int execute_job(job_info *job)
{
    int i = 0, inprevpipe = 0, inpipe = 0, outpipe = 1;
    int end = job->nprocs - 1;
    int retval = 0;
    proc_info *p = NULL;

    CIRCLEQ_FOREACH(p, &job->proc_info_list, link)
    {
        if (i >=0 && i < end)
        {
            int fd[2] = {0};
            retval = pipe(fd);
            if(retval < 0)
            {
                retval = -errno;
                fprintf(stderr, "-xssh:%s(%d) error pipe", __FUNCTION__, __LINE__);
                goto done;
            }

            inpipe = fd[0];
            outpipe = fd[1];
        }
        else
        {
            inpipe  = 0; 
            outpipe = 1;
        }

        int pid = fork();
        if(pid < 0)
        {
            retval = -errno;
            fprintf(stderr, "-xssh:%s(%d) error fork", __FUNCTION__, __LINE__);
            goto done;
        }

        if(pid == 0)
        {
            retval = setpgid(getpid(), i ? job->pgid : 0);
            if(retval < 0)
            {
                //fprintf(stderr, "-xssh:%s(%d) error setpgid", __FUNCTION__, __LINE__);
                exit(-errno);
            }

            if(i == 0 && !job->background)
            {
                /* a) Only those processes which are part of terminal's foreground process group shall be 
                 *   able to read from terminal (e.g. STDIN).
                 * 
                 * b) If any process's group is not terminal's foreground process group and that process performs any read using STDIN
                 *    then it will receive SIGTTIN signal and if that process has not handled that signal
                 *    then it will get another signal SIGSTOP which eventually leads to process being stopped.
                 *
                 * c) So if child process is supposed to be run in foreground then it must it's process group as
                 *    terminal's foreground process group using tcsetpgrp
                 *
                 * d) Once child process's process group becomes terminal foreground process group.
                 *    Not even XSSH can read input from STDIN. So we must be careful before XSSH perform any input I/O
                 *    it must bring it's process group to terminal's foreground process group
                 */


                signal(SIGTTIN, SIG_IGN);  
                signal(SIGTTOU, SIG_IGN);  
                
                //setting this process group in terminal foreground process group
                retval = tcsetpgrp(STDIN_FILENO, getpgrp());
                if(retval < 0)
                {
                    fprintf(stderr, "-xssh:%s(%d) error tcsetpgrp", __FUNCTION__, __LINE__);
                    signal(SIGTTIN, SIG_DFL);
                    signal(SIGTTOU, SIG_DFL);
                    exit(-errno);
                }

                signal(SIGTTIN, SIG_DFL);  
                signal(SIGTTOU, SIG_DFL);  
            }
            
            run_exec(inprevpipe, inpipe, outpipe, p);  //run_exec will either suceed or do exit
        }

        retval = setpgid(pid, i ? job->pgid : 0);
        if(retval < 0 && errno != EACCES)
        {
            fprintf(stderr, "-xssh:%s(%d) error setpgid", __FUNCTION__, __LINE__);
            goto done;
        }

        retval = 0;        
        job->pgid = i ? job->pgid : pid;
        job->lastpid = pid;
        p->pid = pid;
        p->state = XSSH_PROC_STATE_RUNNING;
        job->nrunning++;
 
        if (i == end)   
        {
            if(inprevpipe != 0)
            {
                close(inprevpipe);
                inprevpipe = 0;
            }
        }
        else if( i > 0)
        {
            retval = dup2(inpipe, inprevpipe);
            if(retval < 0)
            {
                fprintf(stderr, "-xssh:%s(%d) error dup2", __FUNCTION__, __LINE__);
                retval = -errno;
                goto done;
            }

            retval = 0;
            close(inpipe);
            close(outpipe);

            inpipe = 0;
            outpipe = 1;
        }
        else
        {   
            //no need to check outpipe since whenever control will come here. outpipe will have valid pipe fd  
            close(outpipe);
            inprevpipe = inpipe;
            inpipe = 0;
            outpipe = 1;
        }
        i++;
    }

done:
    if(inprevpipe != 0)
        close(inprevpipe);
    if(inpipe != 0)
        close(inpipe);
    if(outpipe != 1)
        close(outpipe);

    return retval; 
}

void wait_job()
{
    siginfo_t info; 
    int exitstatus = 0;

    while(g_context.fg_job)
    { 
        int retval = waitid(P_PGID, g_context.fg_job->pgid, &info, WEXITED | WSTOPPED | WCONTINUED);
        if(retval < 0 && errno == ECHILD)
            break;    

        int s_status = info.si_status;
        if(info.si_code == CLD_EXITED)
            process_terminated(g_context.fg_job, info.si_pid, info.si_status);

        if(info.si_code == CLD_STOPPED)
            process_stopped(g_context.fg_job, info.si_pid);

        if(info.si_code == CLD_KILLED)
        {
           process_killed(g_context.fg_job, info.si_pid, info.si_status);
        }
        
        if(info.si_code == CLD_CONTINUED)
        {
            process_continued(g_context.fg_job, info.si_pid);
            fg_job_continued();
        }

        if((g_context.fg_job->state == XSSH_JOB_STATE_STOPPED))
        {
            fg_job_stopped();
            break;
        }
    
        if((g_context.fg_job->state == XSSH_JOB_STATE_KILLED) || (g_context.fg_job->state == XSSH_JOB_STATE_DONE))
        {
            if(info.si_code == CLD_KILLED && info.si_status == SIGINT)
                printf("-xssh: Exit pid %d\n", g_context.fg_job->pgid);  
            fg_job_terminated();
            break;
        }

    }

    //if(g_context.fg_job)
      //  fg_job_terminated();
    wait_background_job(0);
}

void wait_background_job(int pstatus)
{
    job_info *job = NULL;
    siginfo_t info; 
    CIRCLEQ_FOREACH(job, &g_context.bg_jobs, link)
    {
        do
        {
            info.si_pid = 0; 
            int retval = waitid(P_PGID, job->pgid, &info, WNOHANG | WEXITED | WSTOPPED | WCONTINUED);
            if(retval < 0 && errno == ECHILD)
                break;
            if(retval < 0)
            {
                fprintf(stderr, "Failed to wait the background process with pgid=%d, errno=%d", job->pgid, errno);
                break;    
            }
        
            if(retval == 0 && !info.si_pid)
                continue;

            if(info.si_code == CLD_EXITED)
                process_terminated(job, info.si_pid, info.si_status);

            if(info.si_code == CLD_STOPPED)
                process_stopped(job, info.si_pid);

            if(info.si_code == CLD_KILLED)
                process_killed(job, info.si_pid, info.si_status);

            if(info.si_code == CLD_CONTINUED)
                process_continued(job, info.si_pid);

            if(job->state == XSSH_JOB_STATE_DONE || job->state == XSSH_JOB_STATE_KILLED)
            {
                job_info *temp = CIRCLEQ_LOOP_PREV(&g_context.bg_jobs, job, link);
                CIRCLEQ_REMOVE(&g_context.bg_jobs, job, link);      
                print_job_status(job);
                destroy_job(job);
                job = temp;
                break;
            }
        }while(info.si_pid);
      
        if(pstatus) 
            print_job_status(job);
    }
}

void print_job_status(job_info *job)
{         
    if(job->state == XSSH_JOB_STATE_DONE || job->state == XSSH_JOB_STATE_KILLED)
        fprintf(stdout, "[%d] %s %d %s\n", job->job_spec, state_str[job->state], job->status, job->cmd);
    else if(job->state == XSSH_JOB_STATE_RUNNING)
    {
        if(job->background) 
            fprintf(stdout, "[%d] %s %s &\n", job->job_spec, state_str[job->state], job->cmd);
        else
            fprintf(stdout, "%s\n", job->cmd);
    }
    else
        fprintf(stdout, "[%d] %s %s\n", job->job_spec, state_str[job->state], job->cmd);
}

void sigint_fg_job()
{
    if(g_context.fg_job)
        kill(-g_context.fg_job->pgid, SIGINT);     
}

void sigtstp_fg_job()
{
    if(g_context.fg_job)
       suspend_job(g_context.fg_job); 
}

void fg_job_stopped()
{
    if(g_context.fg_job)
    {
        send_job_to_bg(g_context.fg_job, 0);
        print_job_status(g_context.fg_job);
    }

    bring_job_to_fg(NULL);
}

void fg_job_terminated()
{
    if(g_context.fg_job)
    {
        if(g_context.fg_job->job_spec && CIRCLEQ_EMPTY(&g_context.bg_jobs))
            g_context.max_bg_job_index = 0;  //no background job or foregroung
        sprintf(varvalue[1], "%d", g_context.fg_job->status);
        destroy_job(g_context.fg_job);
    } 
    bring_job_to_fg(NULL);
}

void fg_job_continued()
{
    /*if(g_context.fg_job)
    {
        print_job_status(g_context.fg_job);
    }*/
}

void suspend_job(job_info *job)
{
    //kill(job->pgid, SIGTSTP);
}

void resume_job(job_info *job)
{
    kill(-job->pgid, SIGCONT);  
}

void send_job_to_bg(job_info *job, int resume)
{
    sprintf(varvalue[2], "%d", job->pgid); 
    if(!job->job_spec)
        job->job_spec = ++g_context.max_bg_job_index;
    g_context.last_bg_job_index = job->job_spec;
    CIRCLEQ_INSERT_TAIL(&g_context.bg_jobs, job, link);
    job->background = 1;
    if(resume)
        resume_job(job);
}

void bring_job_to_fg(job_info *job)
{
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    tcsetpgrp(STDIN_FILENO, job ? job->pgid: getpgrp());

    g_context.fg_job = job;
    if(g_context.fg_job)
    {
        resume_job(g_context.fg_job);
        g_context.fg_job->background = 0;
        //print_job_status(g_context.fg_job);
    }

    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
}


void process_stopped(job_info *job, pid_t pid)
{
    int found = 0;
    proc_info *p = NULL;
    CIRCLEQ_FOREACH(p, &job->proc_info_list, link)
    {
        if(p->pid == pid)
        {
            found = 1;
            break;
        } 
    }

    if(found)
    {
        if(p->state == XSSH_PROC_STATE_RUNNING)
        {
            job->nrunning--;
            job->nstopped++;
            p->state = XSSH_PROC_STATE_STOPPED;
        }

        if(job->nrunning == 0)
            job->state = XSSH_JOB_STATE_STOPPED;
    }
}

void process_continued(job_info *job, pid_t pid)
{
    int found = 0;
    proc_info *p = NULL;
    CIRCLEQ_FOREACH(p, &job->proc_info_list, link)
    {
        if(p->pid == pid)
        {
            found = 1;
            break;
        } 
    }    

    if(found)
    {
        if(p->state == XSSH_PROC_STATE_STOPPED)
        {
            job->nrunning++;
            job->nstopped--;
            p->state = XSSH_PROC_STATE_RUNNING;
        }

        if(job->nrunning > 0)
            job->state = XSSH_JOB_STATE_RUNNING;
    }
 
}

void process_killed(job_info *job, pid_t pid, int signal)
{
    int found = 0;
    proc_info *p = NULL;
    CIRCLEQ_FOREACH(p, &job->proc_info_list, link)
    {
        if(p->pid == pid)
        {
            found = 1;
            break; 
        } 
    }

    if(found)
    {
        if(p->state == XSSH_PROC_STATE_STOPPED)
            job->nstopped--;

        if(p->state == XSSH_PROC_STATE_RUNNING)
            job->nrunning--;
        
        p->state = XSSH_PROC_STATE_KILLED;

        if(job->nrunning > 0)
            job->state = XSSH_JOB_STATE_RUNNING;
        else
            job->state = XSSH_JOB_STATE_STOPPED;


        //Each killed process shall be removed from the job proc list 
        CIRCLEQ_REMOVE(&job->proc_info_list, p, link);
        job->nprocs--;
        job->status = signal;

        if(job->nprocs == 0)
            job->state = XSSH_JOB_STATE_KILLED;
    } 
}

void process_terminated(job_info *job, pid_t pid, int status)
{
    int found = 0;
    proc_info *p = NULL;
    CIRCLEQ_FOREACH(p, &job->proc_info_list, link)
    {
        if(p->pid == pid)
        {
            found = 1;
            break; 
        } 
    }

   if(found)
    {
        if(p->state == XSSH_PROC_STATE_STOPPED)
            job->nstopped--;

        if(p->state == XSSH_PROC_STATE_RUNNING)
            job->nrunning--;

        p->state = XSSH_PROC_STATE_TERMINATED;

        if(job->nrunning > 0)
            job->state = XSSH_JOB_STATE_RUNNING;
        else
            job->state = XSSH_JOB_STATE_STOPPED;

        CIRCLEQ_REMOVE(&job->proc_info_list, p, link); 
        job->nprocs--;
        job->status = status;

        if(job->nprocs == 0)
            job->state = XSSH_JOB_STATE_DONE;
    } 
}

