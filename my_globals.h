#ifndef MY_GLOBALS
#define MY_GLOBALS

#include    "stdio.h"
#include	"stdlib.h"
#include    "global.h"

///PCB State Information
#define	        CREATE		       100
#define			READY			   101
#define			RUNNING			   102
#define         SUSPEND            103
#define			SLEEPING		   104
#define         SWAPPED            105
#define			TERMINATE		   106

// PCB information
#define			MAX_PROCESSES      10
#define			MAX_NAME	       16
#define			MAX_MSG			   64
#define			MAX_MSG_COUNT	   10

// PROCESS PRIORITY LIMITS
#define         MIN_PRIORITY        0
#define         DEFAULT_PRIORITY    50
#define         MAX_PRIORITY        100

// PROCESS SUSPEND REASONS
#define         WAITING_UNDEFINED   0
#define         WAITING_FOR_MESSAGE 1
#define         WAITING_FOR_DISK    2

// LOCK STATES
#define         DO_LOCK                     1
#define         DO_UNLOCK                   0
#define         SUSPEND_UNTIL_LOCKED        TRUE
#define         DO_NOT_SUSPEND              FALSE
#define         DISK_READ                   1
#define         DISK_WRITE                  2

typedef struct {
    INT16 msg_buffer[MAX_MSG];
    INT32 source_pid;
    INT32 length;
    BOOL handled;
    BOOL broadcast_message;
} MESSAGE;

typedef struct {
    BOOL in_use;
    INT32 pid;
    INT32 page_id;
    INT32 frame_id;
} FRAME;

typedef struct {
    INT16 disk_id;
    INT32 sector_id;
    INT32 buffer[PGSIZE];
    int disk_operation;
} DISK;

typedef struct {
    INT32       pid;
    INT32       delay;
    char        name[MAX_NAME];
    INT32       parent;
    INT32       state;
    INT32       mode;
    INT32       priority;
    INT32       suspend_reason;
    void*       context;
    long        time_spent_processing;
    MESSAGE*    inbound_messages[MAX_MSG_COUNT];
    UINT16      pagetable[VIRTUAL_MEM_PGS];
    DISK*       disk_data;
} PCB;

typedef void* func_ptr;

//******** Function Prototypes *********//
PCB* os_make_process(char* name, INT32 priority, INT32* error, void* entry_point, INT32 mode);
void os_destroy_process(PCB* pcb);
void switch_context( PCB* pcb, short context_mode);
void pcb_cascade_delete_by_parent(INT32 parent_pid);
void dispatcher(void);
void sleep_process(INT32 sleep_time, PCB* sleeping_process);
func_ptr get_function_handle(char *name);
void scheduler_printer(char* action);
void memory_printer();
BOOL enqueue_message(PCB* target_process, MESSAGE* inbound_message);
MESSAGE* remove_message(PCB* pcb, int index);
int find_message_by_source(PCB* pcb, int source_pid);
void clear_handled_broadcast_message();
int find_handled_message(PCB* pcb);
void lock_timer(void);
void unlock_timer(void);
void lock_ready(void);
void unlock_read(void);
void lock_suspend(void);
void unlock_suspend(void);
UINT16 find_empty_frame(INT32 status);
UINT16 page_replacement(INT32 frame);
int get_disk_status(long disk_id);
void disk_read(long disk_id, long sector_id, char* read_buffer);
void disk_write(long disk_id, long sector_id, char* write_buffer);

#endif