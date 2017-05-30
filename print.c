#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parse.h"

static int print_process(process* pr) {

    int index = 0;

    if(pr->program_name == NULL) {
        return -1;
    }

    printf("* program = %s\n", pr->program_name);
    printf("      pid = %d\n", (int)pr->pid);

    if(pr->argument_list != NULL) {
        while(pr->argument_list[index] != NULL) {
                printf( "  - arg[ %d ] = %s\n", index, pr->argument_list[index]);
                index++;
        }
    }

    if(pr->input_redirection != NULL) {
        printf("  - input redirection = %s\n", pr->input_redirection);
    }

    if (pr->output_redirection != NULL)
        printf("  - output redirection = %s [ %s ]\n",
               pr->output_redirection,
               pr->output_option == TRUNC ? "trunc" : "append" );

    return 0;
}

void print_job_list(job* job_list) {
    int      index;
    job*     jb;
    process* pr;
    char mode[32];

    for(index = 0, jb = job_list; jb != NULL; jb = jb->next, ++index) {

        switch(jb->mode){
                  case FOREGROUND: 
                    strcpy(mode,"foreground");
                    break; 
                  case BACKGROUND: 
                    strcpy(mode,"background");
                    break;
                  case STOPPED   : 
                    strcpy(mode,"stopped");
                    break;
                  case DEFUNCT    : 
                    strcpy(mode,"defunct");
                    break;
                  default: 
                    strcpy(mode,"error");
        }

        printf("id %d [ %s ]\n", index,mode);
        printf("pgid = %d,job_num = [%d]\n",(int)jb->pgid,jb->job_num);

        for(pr = jb->process_list; pr != NULL; pr = pr->next) {
            if(print_process( pr ) < 0) {
                // printf("hello\n");
                    exit(EXIT_FAILURE);
            }
            if(jb->next != NULL) {
                printf( "\n" );
            }
        }
 
    }
}
