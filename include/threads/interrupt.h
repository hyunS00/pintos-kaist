#ifndef THREADS_INTERRUPT_H
#define THREADS_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>

/* Interrupts on or off? */
enum intr_level {
	INTR_OFF,             /* Interrupts disabled. */
	INTR_ON               /* Interrupts enabled. */
};

enum intr_level intr_get_level (void);
enum intr_level intr_set_level (enum intr_level);
enum intr_level intr_enable (void);
enum intr_level intr_disable (void);

/* Interrupt stack frame. */
struct gp_registers {
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t r11;
	uint64_t r10;
	uint64_t r9;
	uint64_t r8;
	uint64_t rsi;
	uint64_t rdi;
	uint64_t rbp;
	uint64_t rdx;
	uint64_t rcx;
	uint64_t rbx;
	uint64_t rax;
} __attribute__((packed));

struct intr_frame {
	/* Pushed by intr_entry in intr-stubs.S.
	   These are the interrupted task's saved registers. */
	/* 	intr-stubs.S의 intr_entry에 의해 푸시됨.
		이는 인터럽트된 작업의 저장된 레지스터입니다. */
	
	/* 	grp_registers 필드에는 유저 공간에서 사용하던 
		일반 목적 레지스터들이 저장됩니다. 
		이 레지스터들은 인터럽트 발생 시 CPU에 의해 
		자동으로 저장되지 않기 때문에, 
		소프트웨어적으로 저장해두는 것입니다. */
	struct gp_registers R;
	uint16_t es;	// 유저 공간의 데이터 세그먼트 레지스터 값을 저장합니다.
	uint16_t __pad1;
	uint32_t __pad2;
	uint16_t ds;	// 유저 공간의 데이터 세그먼트 레지스터 값을 저장합니다.
	uint16_t __pad3;
	uint32_t __pad4;

	/* Pushed by intrNN_stub in intr-stubs.S. */
	/* intr-stubs.S의 intrNN_stub에 의해 푸시됨. */
	
	/* 	발생한 인터럽트의 종류를 식별하기 위해 사용됩니다.
		예를 들어, 페이지 폴트, 시스템 호출 등의 
		인터럽트 종류가 있습니다. */
						/* 인터럽트 벡터 번호. */
	uint64_t vec_no; /* Interrupt vector number. */

/* Sometimes pushed by the CPU,
   otherwise for consistency pushed as 0 by intrNN_stub.
   The CPU puts it just under `eip', but we move it here. */
/* 	때때로 CPU에 의해 푸시되며,
	그렇지 않으면 일관성을 위해 intrNN_stub에 의해 0으로 푸시됩니다.
	CPU는 이를 `eip' 바로 아래에 놓지만, 우리는 이것을 여기로 이동시킵니다. */	
	
	/* 	특정 인터럽트(예: 페이지 폴트)와 
		관련된 오류 코드를 저장합니다. 
		이 코드도 유저 공간의 정보를 
		포함하고 있을 수 있습니다. */
	uint64_t error_code;

/* Pushed by the CPU.
   These are the interrupted task's saved registers. */
/* 	CPU에 의해 푸시됨.
	이는 인터럽트된 작업의 저장된 레지스터입니다. */	

/* 여기 5개의 값들은 User Space에서 호출한 인터럽트
	명령어 내부에서 이 5개의 레지스터를 Push하고
	이 것들을 제외한 나머지 값들은 
	interrupt handler내부에서 값을 Push해줌 */
	uintptr_t rip; 	//유저 공간에서 인터럽트가 발생한 명령어의 주소를 저장합니다. 이는 유저 프로그램이 실행 중이던 위치를 나타냅니다.
	uint16_t cs;	//코드 세그먼트 레지스터. 유저 공간에서의 세그먼트 값을 나타냅니다.
	uint16_t __pad5;//정렬을 위한 패딩값
	uint32_t __pad6;//정렬을 위한 패딩값
	uint64_t eflags;//플래그 레지스터. 인터럽트 발생 시점의 CPU 플래그 상태를 나타냅니다.
	uintptr_t rsp;	//스택 포인터. 유저 공간에서의 스택 위치를 나타냅니다.
	uint16_t ss;	//스택 세그먼트 레지스터. 유저 공간에서의 스택 세그먼트를 나타냅니다.
	uint16_t __pad7;//정렬을 위한 패딩값
	uint32_t __pad8;//정렬을 위한 패딩값
} __attribute__((packed));

typedef void intr_handler_func (struct intr_frame *);

void intr_init (void);
void intr_register_ext (uint8_t vec, intr_handler_func *, const char *name);
void intr_register_int (uint8_t vec, int dpl, enum intr_level,
                        intr_handler_func *, const char *name);
bool intr_context (void);
void intr_yield_on_return (void);

void intr_dump_frame (const struct intr_frame *);
const char *intr_name (uint8_t vec);

#endif /* threads/interrupt.h */
