#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CAPACITY 32
#define LOW_BATTERY 20

typedef enum { CAMERA, SONAR, HYDROPHONE, SYSTEM } Sensor;
typedef struct {
    int id, priority, valid, battery_update, comm_update;
    Sensor sensor;
    char type[40], details[100];
    time_t created;
} Event;

static Event queue[CAPACITY];
static int count=0, closed=0, next_id=1;
static pthread_mutex_t qlock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t qready=PTHREAD_COND_INITIALIZER;
static pthread_mutex_t state_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_lock=PTHREAD_MUTEX_INITIALIZER;
static int battery=85, comms=1, degraded=0;
static FILE *logfile;

static const char *sensor_name(Sensor s) {
    switch(s) { case CAMERA:return "CAMERA"; case SONAR:return "SONAR";
    case HYDROPHONE:return "HYDROPHONE"; default:return "SYSTEM"; }
}
static void pause_ms(long ms) {
    struct timespec ts={ms/1000, (ms%1000)*1000000L}; nanosleep(&ts,NULL);
}
static void log_event(const Event *e,const char *status,const char *details) {
    char stamp[32]; struct tm tmv; time_t now=time(NULL);
    localtime_r(&now,&tmv); strftime(stamp,sizeof stamp,"%Y-%m-%d %H:%M:%S",&tmv);
    pthread_mutex_lock(&log_lock);
    if(logfile) { fprintf(logfile,"\"%s\",%d,%s,%s,%d,%s,\"%s\"\n",
        stamp,e->id,sensor_name(e->sensor),e->type,e->priority,status,details); fflush(logfile); }
    pthread_mutex_unlock(&log_lock);
}
static Event create_event(Sensor s,int p,int valid,int batt,int comm,const char *type,const char *details) {
    Event e; memset(&e,0,sizeof e);
    pthread_mutex_lock(&state_lock); e.id=next_id++; pthread_mutex_unlock(&state_lock);
    e.sensor=s; e.priority=p; e.valid=valid; e.battery_update=batt; e.comm_update=comm;
    snprintf(e.type,sizeof e.type,"%s",type); snprintf(e.details,sizeof e.details,"%s",details);
    e.created=time(NULL); return e;
}
static void enqueue(Event e) {
    pthread_mutex_lock(&qlock);
    if(count<CAPACITY) { queue[count++]=e; pthread_cond_signal(&qready); }
    else printf("[QUEUE WARNING] Queue full; event %s was not queued.\n",e.type);
    pthread_mutex_unlock(&qlock);
    printf("[SENSOR INPUT] %s -> %s (priority %d)\n",sensor_name(e.sensor),e.type,e.priority);
    log_event(&e,"QUEUED",e.details);
}
static int dequeue(Event *out) {
    pthread_mutex_lock(&qlock);
    while(count==0 && !closed) pthread_cond_wait(&qready,&qlock);
    if(count==0 && closed) { pthread_mutex_unlock(&qlock); return 0; }
    int best=0;
    for(int i=1;i<count;i++) if(queue[i].priority<queue[best].priority) best=i;
    *out=queue[best];
    for(int i=best;i<count-1;i++) queue[i]=queue[i+1];
    count--; pthread_mutex_unlock(&qlock); return 1;
}
static void process_event(Event *e) {
    pthread_mutex_lock(&state_lock);
    if(e->battery_update>=0) battery=e->battery_update;
    if(e->comm_update>=0) comms=e->comm_update;
    if(e->sensor==HYDROPHONE && !e->valid) degraded=1;
    if(strcmp(e->type,"SENSOR_RECOVERED")==0) degraded=0;
    int b=battery,c=comms,d=degraded;
    pthread_mutex_unlock(&state_lock);

    printf("\n[SCHEDULER] Running %s (priority %d; sensor %s)\n",
           e->type,e->priority,sensor_name(e->sensor));
    const char *status="PROCESSED"; const char *details=e->details;
    if(!e->valid) {
        printf("[FAULT HANDLER] %s data unavailable/invalid. Do not assume the pipeline is safe.\n",sensor_name(e->sensor));
        status="DEGRADED";
    } else if(strcmp(e->type,"OBSTACLE_AHEAD")==0) {
        printf("[PERCEPTION] Sonar indicates a possible obstacle ahead.\n[ALERT] Critical obstacle warning recorded.\n");
        status="ALERT_CREATED"; details="Possible obstacle; operator review required";
    } else if(strcmp(e->type,"SUSPECTED_PIPELINE_DAMAGE")==0) {
        printf("[PERCEPTION] Visual data flagged a possible surface irregularity.\n[ALERT] Suspected anomaly recorded (not a confirmed leak).\n");
        status="ALERT_CREATED"; details="Suspected anomaly; operator review required";
    } else if(strcmp(e->type,"LOW_BATTERY")==0) {
        printf("[POWER MANAGER] Battery low: defer noncritical background work.\n"); status="POWER_CONSERVE";
    } else if(strcmp(e->type,"COMMUNICATION_LOST")==0) {
        printf("[ALERT MANAGER] Surface link offline; retain critical alerts locally.\n"); status="ALERT_QUEUED";
    } else if(strcmp(e->type,"COMMUNICATION_RESTORED")==0) {
        printf("[ALERT MANAGER] Link restored; queued alerts can be retried.\n"); status="LINK_RESTORED";
    } else if(strcmp(e->type,"ROUTINE_PIPELINE_SCAN")==0) {
        printf("[PERCEPTION] Routine pipeline observation processed%s.\n",b<=LOW_BATTERY?" with reduced processing":"");
    } else if(strcmp(e->type,"BACKGROUND_MAINTENANCE")==0 && b<=LOW_BATTERY) {
        printf("[SCHEDULER] Background maintenance deferred to conserve power.\n"); status="DEFERRED";
    } else if(strcmp(e->type,"SENSOR_RECOVERED")==0) {
        printf("[FAULT HANDLER] Sensor recovery recorded.\n"); status="RECOVERED";
    } else printf("[OS CORE] %s\n",e->details);

    printf("[OS STATUS] Battery=%d%% | Communication=%s | Mode=%s\n",
           b,c?"AVAILABLE":"OFFLINE",d?"DEGRADED":(b<=10?"CRITICAL_POWER":(b<=LOW_BATTERY?"POWER_CONSERVE":"NORMAL")));
    log_event(e,status,details);
}
static void *sensor_simulator(void *unused) {
    (void)unused;
    struct Scenario { Sensor s; int p,valid,batt,comm; const char *type,*details; } events[]={
      {CAMERA,3,1,-1,-1,"ROUTINE_PIPELINE_SCAN","Routine camera observation"},
      {SYSTEM,4,1,-1,-1,"BACKGROUND_MAINTENANCE","Noncritical log maintenance"},
      {SONAR,1,1,-1,-1,"OBSTACLE_AHEAD","Possible obstacle ahead"},
      {CAMERA,1,1,-1,-1,"SUSPECTED_PIPELINE_DAMAGE","Possible visual irregularity"},
      {SYSTEM,2,1,18,-1,"LOW_BATTERY","Battery estimate is 18 percent"},
      {HYDROPHONE,2,0,-1,-1,"HYDROPHONE_TIMEOUT","Hydrophone feed timed out"},
      {SYSTEM,2,1,-1,0,"COMMUNICATION_LOST","Surface link unavailable"},
      {CAMERA,3,1,-1,-1,"ROUTINE_PIPELINE_SCAN","Routine observation while offline"},
      {SYSTEM,2,1,-1,1,"COMMUNICATION_RESTORED","Surface link restored"},
      {SYSTEM,2,1,45,-1,"SENSOR_RECOVERED","Sensor recovery demonstration"},
      {SYSTEM,4,1,-1,-1,"BACKGROUND_MAINTENANCE","Background maintenance after recovery"}
    };
    size_t n=sizeof(events)/sizeof(events[0]);
    for(size_t i=0;i<n;i++) {
        pause_ms(350);
        Event e=create_event(events[i].s,events[i].p,events[i].valid,events[i].batt,
                             events[i].comm,events[i].type,events[i].details);
        enqueue(e);
    }
    pthread_mutex_lock(&qlock); closed=1; pthread_cond_broadcast(&qready); pthread_mutex_unlock(&qlock);
    return NULL;
}
int main(void) {
    pthread_t producer; Event e;
    logfile=fopen("underwater_os_log.csv","w");
    if(logfile) fprintf(logfile,"timestamp,event_id,sensor,event_type,priority,status,details\n");
    printf("====================================================\n");
    printf(" NEUROMORPHIC OS FOR UNDERWATER ROBOTIC PERCEPTION\n");
    printf(" Use case: Subsea oil/gas pipeline inspection\n");
    printf("====================================================\n");
    printf("OS concepts: priority scheduling, pthreads, mutex, condition variable,\n");
    printf("             simulated battery management, fault handling, event logging\n\n");
    if(pthread_create(&producer,NULL,sensor_simulator,NULL)!=0) {
        perror("pthread_create"); if(logfile) fclose(logfile); return EXIT_FAILURE;
    }
    while(dequeue(&e)) { process_event(&e); pause_ms(150); }
    pthread_join(producer,NULL);
    printf("\nSIMULATION COMPLETE. Check underwater_os_log.csv for event history.\n");
    printf("Note: simulated results only; suspected damage is not a confirmed leak.\n");
    if(logfile) fclose(logfile);
    pthread_mutex_destroy(&qlock); pthread_cond_destroy(&qready);
    pthread_mutex_destroy(&state_lock); pthread_mutex_destroy(&log_lock);
    return EXIT_SUCCESS;
}
