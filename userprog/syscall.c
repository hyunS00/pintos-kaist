#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"
#include "userprog/process.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void check_address (void *addr);
struct thread *get_child_process(int pid);
void remove_child_process(struct thread *child);


/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	lock_init(&filesys_lock);
}

/* The main system call interface */
/*syscall_handler를 호출할 때 이미 인터럽트 프레임에 
해당 시스템 콜 넘버에 맞는 인자 수만큼 들어있다. 
그러니 각 함수별로 필요한 인자 수만큼 인자를 넣어준다. 
이때 rdi, rsi, ...얘네들은 특정 값이 있는 게 아니라 그냥 인자를 담는 그릇의 번호 순서이다. 
어떤 특정 인자와 매칭되는 게 아니라 
첫번째 인자면 rdi, 두번째 인자면 rsi 이런 식이니 헷갈리지 말 것.
*/
void
syscall_handler (struct intr_frame *f UNUSED) {
	int sys_number = f->R.rax;

	switch (sys_number)
	{
	case SYS_HALT: 							// 운영체제 종료
		halt();
		break;
	case SYS_EXIT:							// 프로그램 종료 후 상태 반환
		exit(f->R.rdi);
		break;
	case SYS_FORK:							// 자식 프로세스 생성
		thread_current()->parent_if = palloc_get_page(0);
		memcpy (thread_current()->parent_if, f, sizeof (struct intr_frame));
		f->R.rax = fork(f->R.rdi);
		break;
	case SYS_EXEC:							// 새 프로그램 실행
		f->R.rax = exec(f->R.rdi);
		break;
	case SYS_WAIT:							// 자식 프로세스가 종료될 때까지 기다림
		f->R.rax = wait(f->R.rdi);
		break;
	case SYS_CREATE:
        f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:						// 파일 삭제
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN:							// 파일 열기
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_FILESIZE:						// 파일 사이즈 반환
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ:							// 파일에서 데이터 읽기
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:							// 파일에 데이터 쓰기
        f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:							// 파일의 읽기/쓰기 포인터 이동
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:							// 파일의 현재 읽기/쓰기 데이터 반환
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE:							// 파일 닫기
		close(f->R.rdi);
		break;
	default:
		exit(-1);
	}
}

/*User memory access는 이후 시스템 콜 구현할 때 메모리에 접근할 텐데, 
이때 접근하는 메모리 주소가 유저 영역인지 커널 영역인지를 체크*/
void check_address (void *addr){
	struct thread *t = thread_current();
	
	/*포인터가 가리키는 주소가 유저영역의 주소인지 확인 
	|| 포인터가 가리키는 주소가 유저 영역 내에 있지만 
	페이지로 할당하지 않은 영역일수도 잇으니 체크*/
	if (!is_user_vaddr(addr) || addr == NULL || pml4_get_page(t->pml4, addr) == NULL){ 	
		exit(-1);	// 잘못된 접근일 경우 프로세스 종료
	}
}

/* pintos 종료시키는 함수 */
void halt(void){
	// printf("halt 실행됐고 pintos 종료\n");
	// filesys_done();
	power_off();
}

/* 현재 프로세스를 종료시키는 시스템 콜 */
void exit(int status){
	struct thread *curr = thread_current();
	curr->exit_status = status;
	curr->is_exit = true;
	thread_exit();
}

int write(int fd, const void *buffer, unsigned size)
{
	int byte_written = size;
	check_address(buffer);
	if(fd == 0) return -1;
	lock_acquire(&filesys_lock);
	if (fd == 1)
	{
		putbuf(buffer, size);
	}
	else
	{
		struct file *f = get_file(fd, thread_current());

		if(f == NULL){
			lock_release(&filesys_lock);
			return -1;
		}
		byte_written = file_write(f, buffer, size);
	}
	lock_release(&filesys_lock);
	return byte_written;
}


/*파일을 제거하는 함수, 
이 때 파일을 제거하더라도 그 이전에 파일을 오픈했다면 
해당 오픈 파일은 close 되지 않고 그대로 켜진 상태로 남아있는다.*/
bool remove (const char *file) {
	check_address(file);
	return filesys_remove(file);
}

/* 파일 생성하는 시스템 콜 */
bool create (const char *file, unsigned initial_size) {
	check_address(file);
	
	bool success;
	/* 성공이면 true, 실패면 false */
	if (file == NULL || initial_size < 0) {
		return 0;
	}

	success = filesys_create(file, initial_size);
	return success;
}

/* open 시스템콜 */
int open (const char *file) {
	struct thread *curr = thread_current();
	check_address(file);
	struct file *open_file = filesys_open(file);
	int fd = -1;

	if(open_file == NULL){
		return -1;
	}
	lock_acquire(&filesys_lock);
	for(int i = 2; i < INT8_MAX; i++){
		if(curr->fd_table[i] == NULL){
			fd = i;
			break;
		}
	}

	if(fd != -1)
		curr->fd_table[fd] = open_file;
	lock_release(&filesys_lock);

	return fd;
}

int read (int fd, void *buffer, unsigned length){
	int size;
	check_address(buffer);

	if(fd == 0){
		return input_getc();
	}
	struct file *f = get_file(fd, thread_current());
	if(f == NULL)
		return 0;

	lock_acquire(&filesys_lock);
	size = file_read(f,buffer,length);
	lock_release(&filesys_lock);
	return size;
}

void close(int fd)
{
	struct file *f = get_file(fd, thread_current());
	lock_acquire(&filesys_lock);
	if(f != NULL)
	{
		file_close(f);
		remove_fd(fd);
	}
	lock_release(&filesys_lock);
}

int filesize (int fd){
	struct file *f = get_file(fd, thread_current());
	off_t size = file_length(f);
	return size;
}

void seek (int fd, unsigned position){
	struct file *f = get_file(fd, thread_current());

	if(f == NULL)
		return;

	file_seek(f, position);
}

unsigned tell (int fd){
	struct file *f = get_file(fd, thread_current());

	if(f == NULL)
		return;

	return file_tell(f);
}

pid_t fork (const char *thread_name){
	return process_fork(thread_name, NULL);
}

int wait (pid_t pid) {
	// printf("--%s wait on->%d\n",thread_name(),pid);
	return process_wait(pid);
}

int exec (const char *file) {
	check_address(file);

	char *fn_copy = palloc_get_page(PAL_ZERO|PAL_USER);
	if ((fn_copy) == NULL) {
		exit(-1);
	}
	strlcpy(fn_copy, file, PGSIZE);
	sema_down(&thread_current()->child_sema);
	if (process_exec(fn_copy) == -1) {
		exit(-1);
	}

	NOT_REACHED();
	return 0;
}