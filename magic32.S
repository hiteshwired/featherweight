# The 32-bit version of context saving.  This is more complicated than
# the 64-bit version, unfortunately.
#
# The goal is to be able to restore the stack to exactly what it looked
# like when we were called	
#
#ifdef __APPLE__
	#define FNAME _swap_rfiles
#else				/* everyone else */
	#define FNAME swap_rfiles
#endif	
	.text
	.globl FNAME
	#ifndef __APPLE__
	.type	swap_rfiles, @function
	#endif
FNAME:
	# void cswitch(rfile *old, rfile *new)
	#
	# We have two parameters: old, new, 
	#
	# The developed stack:
	#       new       +12
	#       old       +8
	#       ra        +4
	# bp->  stored bp  0
	#       saved eax -4
	#       saved ebx -8
	# First, set up a frame pointer and push %e[ab]x to give
	# us a couple of registers to work with.
	#
	# After that, store stuff.
	#
	pushl %ebp
	movl  %esp, %ebp
	pushl %eax
	pushl %ebx

	# save the old context (if old != NULL)
	movl    8(%ebp),%eax	# load "old"
	cmpl	$0,%eax		# skip if NULL
	je load
	
	# store the context
	movl -4(%ebp),%ebx	#save the saved eax to structure
	movl %ebx,(%eax)

	movl -8(%ebp),%ebx	#save the saved ebx to structure
	movl %ebx,4(%eax)

	movl %ecx,8(%eax)	# ecx
	movl %edx,12(%eax)	# edx
	movl %esi,16(%eax)	# esi
	movl %edi,20(%eax)	# edi
	movl %ebp,24(%eax)	# ebp
	movl %ebp,28(%eax)	# esp


load:	# restore the new context (if new != NULL)
	movl   	12(%ebp),%eax	# load "new"
	cmpl	$0,%eax		# skip if NULL
	je done

	movl  4(%eax), %ebx	# ebx
	movl  8(%eax), %ecx	# ecx
	movl 12(%eax), %edx	# edx
	movl 16(%eax), %esi	# esi
	movl 20(%eax), %edi	# edi
	movl 24(%eax), %ebp	# ebp
	movl 28(%eax), %esp	# esp
	movl  0(%eax), %eax	# eax (this one last)
	
done:	leave
	ret
