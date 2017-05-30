#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include "parse.h"


void 	print_job_list(job*);
job 	*run_job(job* curr_jobr,job *job_list,char **envp);
void 	path_exec(process* curr_proc,char** envp);
job  	*add_joblist(job *job_list,job *curr_job);
void  	wait_jobs(job *job_list);
job  	*free_jobs(job *job_list);
// 組み込みコマンド
// exit以外
void  	bg(process *,job *);
void  	fg(process *,job *);
void    jobs(job *);
// シグナル関係
int 	signal_handler_control(int);
void 	sig_handler(int);

int latest_job_num = 0;


int main(int argc, char *argv[], char *envp[]) {
    char s[LINELEN];
    job *curr_job;
    job *job_list = NULL;

    // シグナルハンドラ登録
    if(signal_handler_control(1) != 0){
    	perror("signal handler set failure");
    }

    // REPL
	while(get_line(s, LINELEN)) {
        if(!strcmp(s, "exit\n")){
        	// 全子プロセスを終了させてから終了
            wait_jobs(job_list);
            for(curr_job = job_list;curr_job != NULL;curr_job = curr_job->next)
            	curr_job->mode = DEFUNCT;
            free_jobs(job_list);
            break;

        }else if(!strcmp(s, "print\n")){//デバッグ用
        	print_job_list(job_list);
        }else{
            curr_job = parse_line(s);

            curr_job = run_job(curr_job,job_list,envp); //job_listは組み込みコマンドに渡すのみ使用

            job_list = add_joblist(job_list,curr_job);

            wait_jobs(job_list);

            job_list = free_jobs(job_list);

            if(job_list == NULL){
            	latest_job_num = 0;
            }

        }
    }// end while get_line
    return 0;
}


/**
 * @brief      新しく読み込んだコマンドを処理
 *             NULL（parse_lineに失敗したら）ならNULLを返す
 *             組み込みコマンドなら実行してジョブ構造体を開放した後NULLを返す
 *             有効コマンドなら実行してそのジョブ構造体へのポインタ（すなわち仮引数curr_jobそのもの）を返す
 *             無効コマンドならKILLしてジョブのモードをDEFUNCTにしてそのポインタを返す。（メモリの開放はfree_jobsに任せる）
 *
 * @param      curr_job  新しく読み込んだジョブ
 * @param      job_list  ishが管理するジョブのリスト（連結リスト） 
 *                       組み込みコマンド実行にのみ使用
 * @param      envp      ish実行時の環境
 * 
 * @return     NULL or curr_job
 */
job *run_job(job *curr_job,job *job_list,char *envp[]){
    int in_redirect_fd = -1,out_redirect_fd = -1;
    process* curr_proc;
    pid_t pid;

    if(curr_job == NULL){
    // コマンド読み込み失敗時
       // exit(0);
   	
   	}else 
   	// 組み込みコマンドbg
   	if( strcmp(curr_job->process_list->program_name,"bg")==0){
	    curr_proc = curr_job->process_list;
	    bg(curr_proc,job_list);
	    free_job(curr_job);
	    return NULL;
	}else 
	// 組み込みコマンドfg
	if( strcmp(curr_job->process_list->program_name,"fg")==0){

	    curr_proc = curr_job->process_list;
        fg(curr_proc,job_list);
	    free_job(curr_job);
	    return NULL;
	}else 
	//組み込みコマンドjobs
	if(strcmp(curr_job->process_list->program_name,"jobs")==0){
		wait_jobs(job_list);
		jobs(job_list);
	    free_job(curr_job);
		return NULL;
	}else
	// その他一般コマンド
	{


        // 全プロセスをfork
	    for(curr_proc = curr_job->process_list;curr_proc != NULL;curr_proc = curr_proc->next){ //process_listは連結リストであり、配列ではない

            // 次のプロセスが存在したら、そのプロセスへのパイプを作成
            if(curr_proc->next != NULL && pipe((curr_proc->next)->pipe) < 0)
                perror("pipe");


            // fork
            if((pid =fork()) < 0){
            	//fork失敗 
                perror("fork");
            }else 
            // 子プロセスの処理
            if(pid == 0){



	            // プロセスグループidの設定
                if(curr_job->process_list == curr_proc ){
                    curr_job->pgid = getpid();
                    setpgid(0,curr_job->pgid);
                    //FOREGROUNDフォアグラウンドに設定
                    if(curr_job->mode == FOREGROUND){
                        tcsetpgrp(STDOUT_FILENO,curr_job->pgid);
                    }
                }else{
                    setpgid(0,curr_job->pgid);
                }



	            // 入力リダイレクトの設定（最初のプロセスに対して、存在すれば）
	            if(curr_proc == curr_job->process_list){
            		if(curr_proc->input_redirection != (char *)NULL){ // 最初ののプロセスで標準入力のリダイレクトが存在したらファイルを開く

                        //ファイルを読み込み専用で開く、失敗したらエラー表示
                        if((in_redirect_fd = open(curr_proc->input_redirection,O_RDONLY)) == -1 ){
                            perror("in_redirect open");
                        }else{
                            if(dup2(in_redirect_fd,0) != 0)
                                perror("dup2 in_redirect");

                            close(in_redirect_fd);
                        }
                    }

	            }else

	            // 入力パイプの設定（二個目以降のプロセス）
	            {
                    // パイプの書き込み側fdを閉じる
                    close(curr_proc->pipe[1]);

                    // 子プロセスの標準入力をパイプの読み込み側に繋ぎ直す
                    if( dup2(curr_proc->pipe[0],0) != 0) perror("dup2 receive");

                    // パイプの読み込み側が標準入力に繋がれたので閉じる
                    close(curr_proc->pipe[0]);
	            }
	            



                //出力パイプの設定（次のプロセスが存在すれば）
                //パイプのはfork前に作成済み
                if(curr_proc->next != NULL){

                    // パイプの読み込み側を閉じる
                    close((curr_proc->next)->pipe[0]);

                    // 子プロセスの標準出力をパイプの書き込み側に繋ぎ直す
                    if(dup2((curr_proc->next)->pipe[1],1) != 1) perror("dup2 send");

                    // パイプの書き込み側が標準出力に繋がれたので閉じる
                    close((curr_proc->next)->pipe[1]);

                }else 
                // 出力リダイレクトの設定（最後のプロセスに対して、存在すれば）
                if(curr_proc->output_redirection != (char *)NULL){ // 標準出力のリダイレクトが存在したらファイルを開く
                    // TRUNC
                    if(curr_proc->output_option == TRUNC){

                    //ファイルを書き込み専用で開く、又は新しく作成、どちらも失敗したらエラー表示
	                    if((out_redirect_fd = open(curr_proc->output_redirection,O_WRONLY|O_CREAT,0644)) == -1){
                                perror("out_redirect open");
                            }else{
                                dup2(out_redirect_fd,1);
                                close(out_redirect_fd);
                            }
                    }else 
                    // APPEND
                    if(curr_proc->output_option == APPEND){
                        //ファイルを書き込み（APPEND）で開く、又は新しく作成、どちらも失敗したらエラー表示
                        if((out_redirect_fd = open(curr_proc->output_redirection,O_WRONLY|O_CREAT|O_APPEND,0644)) == -1){
                            perror("out_redirect open");
                        }else{
                            dup2(out_redirect_fd,1);
                            close(out_redirect_fd);
                        }
                    }
                }                    
                // signalの設定をデフォルトに戻す。
                signal_handler_control(0);
                curr_proc->state=RUNNING;
                // コマンドを実行
                path_exec(curr_proc,envp);
                // path_execに失敗したら（コマンドが存在しなかった場合）
                printf("%s: コマンドが見つかりません\n",curr_proc->program_name);

                curr_job->mode =DEFUNCT;
                exit(0);//失敗した場合（実行可能パスが見つからなかった場合）プロセスを終了
            			//これを忘れていてishが増殖した
            }else
            // 親プロセスでの処理
            {
                    // 無駄なパイプを閉じる。これを忘れていて数時間溶かした...
                    close(curr_proc->pipe[0]);
                    close(curr_proc->pipe[1]);
                    // 初めのプロセスでプロセスグループIDを登録
                    if(curr_job->pgid == (pid_t)0){
                        curr_job->pgid = pid;
                    }
                    // プロセスIDの登録
                    curr_proc->pid = pid;
            }//end fork
        }//end of for 
        //バックグラウンドジョブ番号管理 
        if(curr_job->mode == BACKGROUND){
        	tcsetpgrp(STDOUT_FILENO,getpid());
	        curr_job->job_num = ++latest_job_num;
	        printf("[%d] %d\n",curr_job->job_num,(int)curr_job->pgid);
        }else
        // 途中で実行に失敗した場合 全プロセスを殺す。
        if(curr_job->mode == DEFUNCT){
        	for(curr_proc = curr_job->process_list;job_list != NULL;curr_proc = curr_proc->next){
        		kill(curr_proc->pid,SIGKILL);
        	}
        }
    }//end of genelal command
    return curr_job;
}//end of run_job definiton





/**
 * @brief      パスを補完してexcecve
 *
 * @param      curr_proc  実行するプロセス
 * @param      envp       実行用の環境
 */
void path_exec(process *curr_proc,char **envp){
    char path[128];
    // 相対パスは補完する
    if(curr_proc->program_name[0] != '/'){
    strcpy(path,"/bin/");
    execve(strcat(path,curr_proc->program_name),curr_proc->argument_list,envp);
    strcpy(path,"/usr/bin/");
    execve(strcat(path,curr_proc->program_name),curr_proc->argument_list,envp);
    }
    execve(curr_proc->program_name,curr_proc->argument_list,envp);
}



/**
 * @brief      組み込みコマンドbg
 *
 * @param      command   再開するジョブの番号はここに格納されている
 * @param      job_list  管理しているジョブのリスト
 */
void bg(process *command,job *job_list){
    // commandがNULL出ないことはrun_jobにより保証されている
    job 	*curr_job;
    process *curr_proc;
    int n = 0;
    if (command->argument_list[1] == NULL){
        n = 0;
    }else{
     	n = atoi(command->argument_list[1]);
    }

    if(latest_job_num == 0 || job_list == NULL){
        printf("そのようなジョブはありません\n");
    }else{
        for(curr_job = job_list;curr_job!= NULL;curr_job = curr_job->next){
            if(curr_job->mode == STOPPED && (curr_job->job_num == n || n == 0)){
                curr_job->mode = BACKGROUND;
                for(curr_proc = curr_job->process_list;curr_proc != NULL; curr_proc = curr_proc->next){
                    kill(curr_proc->pid,SIGCONT);                      
                }
                break;
            }
        }
        if(curr_job == NULL){
            printf("引数に指定したジョブが存在しないか、停止していない可能性があります。\n");
        }
    }
}



/**
 * @brief      組み込みコマンドfg
 *
 * @param      command   再開するジョブの番号はここに格納されている
 * @param      job_list  The job list
 */
void fg(process *command,job *job_list){
    job 	*curr_job;
    process *curr_proc;
    int n = 0;
    if (command->argument_list[1] == NULL){
        n = 0;
    }else{
     	n = atoi(command->argument_list[1]);
    }

    if(latest_job_num == 0 || job_list == NULL){
        printf("そのようなジョブはありません\n");
    }else{
        for(curr_job = job_list;curr_job!= NULL;curr_job = curr_job->next){
            if((curr_job->mode == STOPPED || curr_job->mode == BACKGROUND) && (curr_job->job_num == n || n == 0) ){
            	tcsetpgrp(STDOUT_FILENO,curr_job->pgid);
                if(curr_job->mode == STOPPED){
                    for(curr_proc = curr_job->process_list;curr_proc != NULL; curr_proc = curr_proc->next){
                        kill(curr_proc->pid,SIGCONT);                      
                    }
                }
                curr_job->mode = FOREGROUND;
                break;
            }
        }
        if(curr_job == NULL){
            printf("そのようなジョブはありません\n");
        }
    }
}



/**
 * @brief      ジョブ一覧の表示
 *
 * @param      job_list  The job list
 */
void jobs(job *job_list){
	if(job_list != NULL){
	switch(job_list->mode){
		case BACKGROUND:
			printf("[%d]   実行中\t  %s\n",job_list->job_num,job_list->command);
			break;
		case STOPPED:
			printf("[%d]   停止\t  %s\n",job_list->job_num,job_list->command);
			break;
		case DEFUNCT:
			printf("[%d]   終了\t  %s\n",job_list->job_num,job_list->command);
			break;
		default:
			printf("error\n");
	}
	jobs(job_list->next);
	}
}

/**
 * @brief      job_listに新しく実行したコマンドを追加
 *
 * @param      job_list  ishが管理するジョブのリスト
 * @param      curr_job  新しく実行した
 *
 * @return     { description_of_the_return_value }
 */
job *add_joblist(job *job_list,job *curr_job){
	if(job_list == (job *)NULL){
        // job_listが空なら新しいジョブをジョブリストとして返す
    	return curr_job;
	}else{
   		job_list->next = add_joblist(job_list->next,curr_job);
	}
	return job_list;
}



/**
 * @brief      管理しているジョブを再帰的に全てwait
 *
 * @param      job_list  The job list
 */
void wait_jobs(job *job_list){
    int     status = 0;
    process *curr_proc;
    if(job_list != NULL){
        // curr_jobの終了をwait(FOREGROUNDのみブロッキング)
        for(curr_proc = job_list->process_list; curr_proc != NULL; curr_proc = curr_proc->next){

        	// BACKGROUND
	        if(job_list->mode == BACKGROUND){
	            waitpid(curr_proc->pid,&status,WNOHANG|WUNTRACED); //ノンブロッキングにwait

	            //BACKGROUNDで停止した場合
	            if(WIFSTOPPED(status)){

	                tcsetpgrp(STDOUT_FILENO,getpid());
	                job_list->mode =STOPPED;
	                curr_proc->state = WAITING;
	                switch(WSTOPSIG(status)){
	                    case SIGTSTP:
	                    printf("[%d]\t停止 %s\tBACKGROUND by SIGTSTP\n",job_list->job_num,job_list->command);
	                    break;
	                    case SIGTTIN:
	                    printf("[%d]\t停止 %s\tBACKGROUND by SIGTTIN\n",job_list->job_num,job_list->command);
	                    break;
	                    case SIGTTOU:
	                    printf("[%d]\t停止 %s\tBACKGROUND by SIGTTOU\n",job_list->job_num,job_list->command);
	                    break;
	                    default:
	                    printf("[%d]\t停止 %s\tBACKGROUND by someSIG\n",job_list->job_num,job_list->command);
	                }


	            }else

	             //BACKGROUNDで終了した場合
	            if(kill(curr_proc->pid ,0) != 0){

	                    // 終了したプロセスの標準入力に対応するパイプの一端を閉じる
	                if (curr_proc->pipe[0] != -1)
	                    close(curr_proc->pipe[0]);

	                    //終了したプロセスの標準出力に対応するパイプの一端を閉じる 
	                    // これをすることで次のプロセスにEOFが送られる。（逆に忘れると次のプロセスの入力でブロックする。）
	                if (curr_proc->next != NULL && (curr_proc->next)->pipe[1] != -1)
	                    close(curr_proc->next->pipe[1]);

	                // ジョブの全てのプロセスが終了したらジョブの状態をDELETEに
	                if (curr_proc->next == NULL){ 
	                    job_list->mode = DEFUNCT;
	                    if(latest_job_num == job_list->job_num){
	                    	latest_job_num--;
	                    }
	                }
	            }
	            // 実行再開（本来は無いはず）
	            // if(WIFCONTINUED(status)){
	            //     perror("not stopped background process continue");
	            // }
            	// それ以外の場合は変化なしとみなして次に進む
        	}else 

	        // FOREGROUND
	        if(job_list->mode == FOREGROUND){
	            waitpid(curr_proc->pid,&status,WUNTRACED);

	            // FOREGROUNDで停止した場合
	            if(WIFSTOPPED(status)){
	                tcsetpgrp(STDOUT_FILENO,getpid());
	                job_list->mode = STOPPED;
	                curr_proc->state = WAITING;
	                job_list->job_num = ++latest_job_num;
	                switch(WSTOPSIG(status)){
	                    case SIGTSTP:
	                    printf("[%d]\t停止 %s\tFOREGROUND by SIGTSTP\n",job_list->job_num,job_list->command);
	                    break;
	                    case SIGTTIN:
	                    printf("[%d]\t停止 %s\tFOREGROUND by SIGTTIN\n",job_list->job_num,job_list->command);
	                    break;
	                    case SIGTTOU:
	                    printf("[%d]\t停止 %s\tFOREGROUND by SIGTTOU\n",job_list->job_num,job_list->command);
	                    break;
	                    default:
	                    printf("[%d]\t停止 %s\tFOREGROUND by someSIG\n",job_list->job_num,job_list->command);
	                }
	            }else 

	            // FOREGROUNDで終了した場合
	            	if(kill(curr_proc->pid ,0) != 0){
	
	                // 終了したプロセスの標準入力に対応するパイプの一端を閉じる
	                if (curr_proc->pipe[0] != -1)
	                    close(curr_proc->pipe[0]);

	                    //終了したプロセスの標準出力に対応するパイプの一端を閉じる 
	                    // これをすることで次のプロセスにEOFが送られる。（逆に忘れると次のプロセスの入力でブロックする。）
	                if (curr_proc->next != NULL && (curr_proc->next)->pipe[1] != -1)
	                    close(curr_proc->next->pipe[1]);

	                // ジョブの全てのプロセスが終了したらジョブの状態をDEFUNCTに
	                if (curr_proc->next == NULL){
	                    job_list->mode = DEFUNCT;
	                    if(latest_job_num == job_list->job_num){
	                    	latest_job_num--;
	                    }
	                }
	                //ishをFOREGROUNDに戻す
	                tcsetpgrp(STDOUT_FILENO,getpid());

	            }
	            //再開した場合（本来は無いはず）
            	// if(WIFCONTINUED(status)){
	            //     perror("not stopped foreground process continue");
            	// }


	        }else 
	        // コマンド実行に失敗してKILLされたプロセスの処理
	        if(job_list->mode == DEFUNCT ){
	        	kill(curr_proc->pid,SIGKILL);//念の為もう一度kill
	        	waitpid(curr_proc->pid,&status,WNOHANG|WUNTRACED|WCONTINUED);
	        	if(WIFEXITED(status)){
	        		// 終了したプロセスの標準入力に対応するパイプの一端を閉じる
	                if (curr_proc->pipe[0] != -1)
	                    close(curr_proc->pipe[0]);

	                    //終了したプロセスの標準出力に対応するパイプの一端を閉じる 
	                    // これをすることで次のプロセスにEOFが送られる。（逆に忘れると次のプロセスの入力でブロックする。）
	                if (curr_proc->next != NULL && (curr_proc->next)->pipe[1] != -1)
	                    close(curr_proc->next->pipe[1]);

	                // ジョブの全てのプロセスが終了したらジョブの状態をDEFUNCTに
	                if (curr_proc->next == NULL) 
	                    job_list->mode = DEFUNCT;
	                //ishをFOREGROUNDに戻す
	                tcsetpgrp(STDOUT_FILENO,getpid());
	            }else{
	            	// perror("defunct process stil alive");
	            }
	        }
        }//end of for
   	// 次のジョブをwait
    wait_jobs(job_list->next);
    }//end of if Not NULL
}//end of wait_jobs definision 





/**
 * @brief      ジョブリストのうち、終了したものを開放
 *
 * @param      job_list  The job list
 *
 * @return     更新されたjob_list
 */
job  *free_jobs(job *job_list){
    job *tmp_job;
    if(job_list == NULL){
    	return NULL;
    }else{
        job_list->next = free_jobs(job_list->next);
        if(job_list->mode == DEFUNCT){
            tmp_job = job_list->next;
            free_job(job_list);
            return tmp_job;
        }else{
            return job_list;
        }
    }
}


/**
 * @brief      シグナルハンドラのセット、リセットを行う
 *
 * @param      is_valid  1ならセット0ならリセット
 *
 * @return     成功で0
 */
int signal_handler_control(int is_valid){

 	struct sigaction act; //sigactionの設定
 	
    memset(&act, 0, sizeof(act));
    //sigsetを初期化
    if (sigemptyset(&(act.sa_mask)) < 0) {
        return 1;
    }

    if(is_valid == 1){
    act.sa_handler = SIG_IGN; //シグナルハンドラを設定
	}else{
    act.sa_handler = SIG_DFL;
	}
    // act.sa_mask = sigset; //シグナルマスクを設定

	act.sa_flags |= SA_RESTART;
	act.sa_flags |= SA_NOCLDWAIT;

    //actの条件でSIGINTの設定をする(無視又はデフォルト)
	if (sigaction(SIGINT, &act, NULL) < 0) {
	    return 1;
	}

	if (sigaction(SIGTSTP, &act, NULL) < 0) {
	    return 1;
	}

	if (sigaction(SIGTTOU, &act, NULL) < 0) {
	    return 1;
	}

	if (sigaction(SIGTTIN, &act, NULL) < 0) {
	    return 1;
	}
	if (sigaction(SIGCHLD, &act, NULL) < 0) {
	    return 1;
	}

    //actの条件でSIGINTの設定をする(シグナルハンドラ)
    if(is_valid == 1){
	act.sa_handler = sig_handler;

	if (sigaction(SIGTTOU, &act, NULL) < 0) {
	    return 1;
	}

	if (sigaction(SIGTTIN, &act, NULL) < 0) {
	    return 1;
	}
	}
	return 0;
}


/**
 * @brief   バックグラウンドで入出力を行おうとしたらフォアグラウンドに回復
 *
 */
void sig_handler(int sig){
	//非同期シグナルセーフ
    tcsetpgrp(STDOUT_FILENO,getpid());
}
