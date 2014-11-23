/* ********************************
 * 
 * Author:  Johan Hanssen Seferidis
 * Date:    12/08/2011
 * Update:  01/11/2011
 * License: LGPL
 * 
 * 
 *//** @file thpool.h *//*
 ********************************/

/* Library providing a threading pool where you can add work. For an example on 
 * usage you refer to the main file found in the same package */

/* 
 * Fast reminders:
 * 
 * tp           = threadpool 
 * thpool       = threadpool
 * thpool_t     = threadpool type
 * tp_p         = threadpool pointer
 * sem          = semaphore
 * xN           = x can be any string. N stands for amount
 * 
 * */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>

#include "thpool.h"      /* here you can also find the interface to each function */


static int thpool_keepalive=1;

/* Create mutex variable */
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; /* used to serialize queue access */


/* Initialise thread pool */
thpool_t* thpool_init(int threadsN){
	thpool_t* tp_p;
	
	if (threadsN<0) threadsN=0;
	
	/* Make new thread pool */
	tp_p=(thpool_t*)malloc(sizeof(thpool_t));                              /* MALLOC thread pool */
	if (tp_p==NULL){
		fprintf(stderr, "thpool_init(): Could not allocate memory for thread pool\n");
		return NULL;
	}
	tp_p->threads=(pthread_t*)malloc(threadsN*sizeof(pthread_t));          /* MALLOC thread IDs */
	if (tp_p->threads==NULL){
		fprintf(stderr, "thpool_init(): Could not allocate memory for thread IDs\n");
		return NULL;
	}
	tp_p->threadsN=threadsN;
	
	/* Initialise the job queue */
	if (jobqueue_init(tp_p)==-1){
		fprintf(stderr, "thpool_init(): Could not allocate memory for job queue\n");
		return NULL;
	}
	
	/* Initialise semaphore*/
	tp_p->queued_jobsN=(sem_t*)malloc(sizeof(sem_t));  /* MALLOC */
	sem_init(tp_p->queued_jobsN, 0, 0);                /* no shared, initial value */
	
	/* Make threads in pool */
	int t;
	for (t=0; t<threadsN; t++){
		printf("Created thread %d in pool \n", t);
		pthread_create(&(tp_p->threads[t]), NULL, (void *)thpool_thread_do, (void *)tp_p);
	}
	
	return tp_p;
}


/* What each individual thread is doing 
 * */
/* There are two scenarios here. One is everything works as it should and second if
 * the thpool is to be killed. In that manner we try to BYPASS sem_wait and end each thread. */
void thpool_thread_do(thpool_t* tp_p){

   sleep(1);

	while(thpool_keepalive){
		
		if (sem_wait(tp_p->queued_jobsN)) {/* WAITING until there is work in the queue */
			perror("thpool_thread_do(): Waiting for semaphore");
			exit(1);
		}

		if (thpool_keepalive){
			
			/* Read job from queue and execute it */
			void*(*func_buff)(void* arg);
			void*  arg_buff;
			job_t* job_p;
	
			pthread_mutex_lock(&mutex);                  /* LOCK */
         puts("pulling job");
			job_p = jobqueue_pull(tp_p);
         pthread_mutex_unlock(&mutex);                /* UNLOCK */

         if (job_p) {
            func_buff=job_p->function;
            arg_buff =job_p->arg;
            func_buff(arg_buff);               		   /* run function */
            free(job_p);                              /* DEALLOC job */
         }
		}
		else
		{
			return; /* EXIT thread*/
		}
	}
	return;
}


/* Add work to the thread pool */
int thpool_add_work(thpool_t* tp_p, void *(*function_p)(void*), void* arg_p){
	job_t* newJob;
	
	newJob=(job_t*)malloc(sizeof(job_t));  /* MALLOC job */
	if (newJob==NULL){
		fprintf(stderr, "thpool_add_work(): Could not allocate memory for new job\n");
		exit(1);
	}
	
	/* add function and argument */
	newJob->function=function_p;
	newJob->arg=arg_p;
	
	/* add job to queue */
	pthread_mutex_lock(&mutex);                  /* LOCK */
	jobqueue_push(tp_p, newJob);
   sem_post(tp_p->queued_jobsN);
   printf("pushed job. queue len: %d\n", jobqueue_len(tp_p));
	pthread_mutex_unlock(&mutex);                /* UNLOCK */
	
	return 0;
}


/* Destroy the threadpool */
void thpool_destroy(thpool_t* tp_p){
	int t;
	
	/* End each thread's infinite loop */
	thpool_keepalive=0; 

	/* Awake idle threads waiting at semaphore */
	for (t=0; t<(tp_p->threadsN); t++){
		if (sem_post(tp_p->queued_jobsN)){
			fprintf(stderr, "thpool_destroy(): Could not bypass sem_wait()\n");
		}
	}

	/* Kill semaphore */
	if (sem_destroy(tp_p->queued_jobsN)!=0){
		fprintf(stderr, "thpool_destroy(): Could not destroy semaphore\n");
	}
	
	/* Wait for threads to finish */
	for (t=0; t<(tp_p->threadsN); t++){
		pthread_join(tp_p->threads[t], NULL);
	}
	
	jobqueue_empty(tp_p);
	
	/* Dealloc */
	free(tp_p->threads);                                                   /* DEALLOC threads             */
	free(tp_p->queued_jobsN);                                        /* DEALLOC job queue semaphore */
	free(tp_p->jobqueue);                                                  /* DEALLOC job queue           */
	free(tp_p);                                                            /* DEALLOC thread pool         */
}



/* =================== JOB QUEUE OPERATIONS ===================== */



/* Initialise queue */
int jobqueue_init(thpool_t* tp_p){
	tp_p->jobqueue=(thpool_jobqueue*)malloc(sizeof(thpool_jobqueue));      /* MALLOC job queue */
	if (tp_p->jobqueue==NULL) return -1;
	tp_p->jobqueue->tail=NULL;
	tp_p->jobqueue->head=NULL;
	return 0;
}


/* How many jobs currently in queue */
int jobqueue_len(thpool_t* tp_p){
   int val;
   sem_getvalue(tp_p->queued_jobsN, &val);
   return val;
}


/* Add job to queue */
void jobqueue_push(thpool_t* tp_p, job_t* newjob_p){ /* remember that job prev and next point to NULL */

   newjob_p->next=NULL;

   //printf("jobqueue_push() ---> sem_jobs: %d\n", jobqueue_len(tp_p));
	switch(jobqueue_len(tp_p)){

		case 0:  /* if there are no jobs in queue */
					tp_p->jobqueue->tail=newjob_p;
					tp_p->jobqueue->head=newjob_p;
					break;

		default: /* if there are already jobs in queue */
               tp_p->jobqueue->tail->next=newjob_p;
               newjob_p->prev=tp_p->jobqueue->tail;
               tp_p->jobqueue->tail=newjob_p;

	}
}


/* Get first element from queue */
job_t* jobqueue_pull(thpool_t* tp_p){
   
   /* get first job */
   job_t* job_p;
   job_p = tp_p->jobqueue->head;

   printf("jobqueue_pull():   queue length: %d\n", jobqueue_len(tp_p));

	/* remove job from queue */
	switch(jobqueue_len(tp_p)){
		
		case 0:  /* if there are no jobs in queue */
					return NULL;
					break;
		
		case 1:  /* if there is only one job in queue */
					tp_p->jobqueue->tail=NULL;
					tp_p->jobqueue->head=NULL;
					break;
					
		default: /* if there are more than one jobs in queue */
               tp_p->jobqueue->head=job_p->next;
               job_p->next->prev=tp_p->jobqueue->head;
	}

	return job_p;
}


/* Remove and deallocate all jobs in queue */
void jobqueue_empty(thpool_t* tp_p){
	
	job_t* curjob;
	curjob=tp_p->jobqueue->tail;
	
	while(jobqueue_len(tp_p)){
		tp_p->jobqueue->tail=curjob->prev;
		free(curjob);
		curjob=tp_p->jobqueue->tail;
	}
	
	/* Fix head and tail */
	tp_p->jobqueue->tail=NULL;
	tp_p->jobqueue->head=NULL;
}
