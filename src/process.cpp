#include <config.h>
#include <ucommon/thread.h>
#include <ucommon/string.h>
#include <ucommon/proc.h>
#include <errno.h>

#ifdef	HAVE_SYSLOG_H
#include <syslog.h>
#endif

#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#ifdef HAVE_MQUEUE_H
#include <mqueue.h>
#endif

#if HAVE_FTOK
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

#ifdef	HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <limits.h>

#ifndef	OPEN_MAX
#define	OPEN_MAX 20
#endif

#if	defined(HAVE_FTOK) && (!defined(HAVE_MQUEUE_H) || !defined(HAVE_SHM_OPEN))

#include <sys/ipc.h>
#include <sys/shm.h>

static void ftok_name(const char *name, char *buf, size_t max)
{
	if(*name == '/')
		++name;

	if(cpr_isdir("/var/run/ipc"))
		snprintf(buf, sizeof(buf), "/var/run/ipc/%s", name);
	else
		snprintf(buf, sizeof(buf), "/tmp/.%s.ipc", name);
}

#if !defined(HAVE_MQUEUE_H)
static void unlinkipc(const char *name)
{
	char buf[65];

	ftok_name(name, buf, sizeof(buf));
	remove(buf);
}
#endif

static key_t createipc(const char *name, char mode)
{
	char buf[65];
	int fd;

	ftok_name(name, buf, sizeof(buf));
	fd = open(buf, O_CREAT | O_EXCL | O_WRONLY, 0660);
	if(fd > -1)
		close(fd);
	return ftok(buf, mode);
}

static key_t accessipc(const char *name, char mode)
{
	char buf[65];

	ftok_name(name, buf, sizeof(buf));
	return ftok(buf, mode);
}

#endif

using namespace UCOMMON_NAMESPACE;

#if defined(_MSWINDOWS_)

MappedMemory::MappedMemory(const char *fn, size_t len)
{
	int share = FILE_SHARE_READ;
	int prot = FILE_MAP_READ;
	int mode = GENERIC_READ;
	struct stat ino;

	size = 0;
	used = 0;
	map = NULL;

	if(len) {
		prot = FILE_MAP_WRITE;
		mode |= GENERIC_WRITE;
		share |= FILE_SHARE_WRITE;
		fd = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, len, fn + 1);

	}
	else
		fd = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, fn + 1);
	
	if(fd == INVALID_HANDLE_VALUE || fd == NULL) 
		return;

	map = (caddr_t)MapViewOfFile(fd, FILE_MAP_ALL_ACCESS, 0, 0, len);
	if(map) {
		size = len;
		VirtualLock(map, size);
	}
}

MappedMemory::~MappedMemory()
{
	if(map) {
		VirtualUnlock(map, size);
		UnmapViewOfFile(fd);
		CloseHandle(fd);
		map = NULL;
		fd = INVALID_HANDLE_VALUE;
	}
}

#elif defined(HAVE_SHM_OPEN)

MappedMemory::MappedMemory(const char *fn, size_t len)
{
	int prot = PROT_READ;
	struct stat ino;

	size = 0;
	used = 0;
	
	if(len) {
		prot |= PROT_WRITE;
		shm_unlink(fn);
		fd = shm_open(fn, O_RDWR | O_CREAT, 0660);
		if(fd > -1)
			ftruncate(fd, len);
	}
	else {
		fd = shm_open(fn, O_RDONLY, 0660);
		if(fd > -1) {
			fstat(fd, &ino);
			len = ino.st_size;
		}
	}

	if(fd < 0)
		return;

	map = (caddr_t)mmap(NULL, len, prot, MAP_SHARED, fd, 0);
	close(fd);
	if(map != (caddr_t)MAP_FAILED) {
		size = len;
		mlock(map, size);
	}
}

MappedMemory::~MappedMemory()
{
	if(size) {
		munlock(map, size);
		munmap(map, size);
		size = 0;
	}
}

#else

MappedMemory::MappedMemory(const char *name, size_t len)
{
	struct shmid_ds stat;
	size = 0;
	used = 0;
	key_t key;

	if(len) {
		key = createipc(name, 'S');
remake:
		fd = shmget(key, len, IPC_CREAT | IPC_EXCL | 0660);
		if(fd == -1 && errno == EEXIST) {
			fd = shmget(key, 0, 0);
			if(fd > -1) {
				shmctl(fd, IPC_RMID, NULL);
				goto remake;
			}
		}
	}
	else {
		key = accessipc(name, 'S');
		fd = shmget(key, 0, 0);
	}
	
	if(fd > -1) {
		if(len)
			size = len;
		else if(shmctl(fd, IPC_STAT, &stat) == 0)
			size = stat.shm_segsz;
		else
			fd = -1;
	}
	map = (caddr_t)shmat(fd, NULL, 0);
#ifdef	SHM_LOCK
	if(fd > -1)
		shmctl(fd, SHM_LOCK, NULL);
#endif
}

MappedMemory::~MappedMemory()
{
	if(size > 0) {
#ifdef	SHM_UNLOCK
		shmctl(fd, SHM_UNLOCK, NULL);
#endif
		shmdt(map);
		fd = -1;
		size = 0;
	}
}

#endif

void MappedMemory::fault(void) 
{
	abort();
}

void *MappedMemory::sbrk(size_t len)
{
	void *mp = (void *)(map + used);
	if(used + len > size)
		fault();
	used += len;
	return mp;
}
	
void *MappedMemory::get(size_t offset)
{
	if(offset >= size)
		fault();
	return (void *)(map + offset);
}

MappedReuse::MappedReuse(const char *name, size_t osize, unsigned count) :
ReusableAllocator(), MappedMemory(name,  osize * count)
{
	objsize = osize;
}

bool MappedReuse::avail(void)
{
	bool rtn = false;
	lock();
	if(freelist || used < size)
		rtn = true;
	unlock();
	return rtn;
}

ReusableObject *MappedReuse::request(void)
{
    ReusableObject *obj = NULL;

	lock();
	if(freelist) {
		obj = freelist;
		freelist = next(obj);
	} 
	else if(used + objsize <= size)
		obj = (ReusableObject *)sbrk(objsize);
	unlock();
	return obj;	
}

ReusableObject *MappedReuse::get(void)
{
	return get(Timer::inf);
}

ReusableObject *MappedReuse::get(timeout_t timeout)
{
	bool rtn = true;
	Timer expires;
	ReusableObject *obj = NULL;

	if(timeout && timeout != Timer::inf)
		expires.set(timeout);

	lock();
	while(rtn && !freelist && used >= size) {
		++waiting;
		if(timeout == Timer::inf)
			wait();
		else if(timeout)
			rtn = wait(*expires);
		else
			rtn = false;
		--waiting;
	}
	if(!rtn) {
		unlock();
		return NULL;
	}
	if(freelist) {
		obj = freelist;
		freelist = next(obj);
	}
	else if(used + objsize <= size)
		obj = (ReusableObject *)sbrk(objsize);
	unlock();
	return obj;
}

#ifdef	HAVE_MQUEUE_H

struct MessageQueue::ipc
{
	mqd_t mqid;
	mq_attr attr;
};

MessageQueue::MessageQueue(const char *name, size_t msgsize, unsigned count)
{
	mq = (ipc *)malloc(sizeof(ipc));
	memset(&mq->attr, 0 , sizeof(mq_attr));
	mq->attr.mq_maxmsg = count;
	mq->attr.mq_msgsize = msgsize;
	mq_unlink(name);
	mq->mqid = (mqd_t)(-1);
	if(strrchr(name, '/') == name)
		mq->mqid = mq_open(name, O_CREAT | O_RDWR | O_NONBLOCK, 0660, &mq->attr);
	if(mq->mqid == (mqd_t)(-1)) {
		free(mq);
		mq = NULL;
	}
}
	
MessageQueue::MessageQueue(const char *name)
{
	mq = (ipc *)malloc(sizeof(ipc));

	mq->mqid = (mqd_t)(-1);
	if(strrchr(name, '/') == name)
		mq->mqid = mq_open(name, O_WRONLY | O_NONBLOCK);
	if(mq->mqid == (mqd_t)-1) {
		free(mq);
		mq = NULL;
		return;
	}
	mq_getattr(mq->mqid, &mq->attr);
}

MessageQueue::~MessageQueue()
{
	release();
}

void MessageQueue::release(void)
{
	if(mq) {
		mq_close(mq->mqid);
		free(mq);
		mq = NULL;
	}
}

unsigned MessageQueue::getPending(void) const
{
	mq_attr attr;
	if(!mq)
		return 0;

	if(mq_getattr(mq->mqid, &attr))
		return 0;

	return attr.mq_curmsgs;
}

bool MessageQueue::puts(char *buf)
{
	size_t len = string::count(buf);
	if(!mq)
		return false;

	if(len >= (size_t)(mq->attr.mq_msgsize))
		return false;
	
	return put(buf, len);
}

bool MessageQueue::put(void *buf, size_t len)
{
	if(!mq)
		return false;

	if(!len)
		len = mq->attr.mq_msgsize;

	if(!len)
		return false;

	if(mq_send(mq->mqid, (const char *)buf, len, 0) < 0)
		return false;

	return true;
}

bool MessageQueue::gets(char *buf)
{
	unsigned int pri;
	ssize_t len = mq->attr.mq_msgsize;
	if(!len)
		return false;

	len = mq_receive(mq->mqid, buf, (size_t)len, &pri);
	if(len < 1)
		return false;
	
	buf[len] = 0;
	return true;
}	

bool MessageQueue::get(void *buf, size_t len)
{
	unsigned int pri;

	if(!mq)
		return false;
	
	if(!len)
		len = mq->attr.mq_msgsize;

	if(!len)
		return false;

	if(mq_receive(mq->mqid, (char *)buf, len, &pri) < 0)
		return false;
	
	return true;
}

#elif defined(HAVE_FTOK)
#include <sys/msg.h>

/* struct msgbuf {
	long mtype;
	char mtext[1];
}; */

struct MessageQueue::ipc
{
	key_t key;
	bool creator;
	int fd;
	struct msqid_ds attr;
	struct msginfo info;
	struct msgbuf *mbuf;
};

MessageQueue::MessageQueue(const char *name)
{
	mq = (ipc *)malloc(sizeof(ipc));
	mq->key = accessipc(name, 'M');
	mq->creator = false;

	mq->fd = -1;
	if(strrchr(name, '/') == name) 
		mq->fd = msgget(mq->key, 0660);

	if(mq->fd == -1) {
fail:
		free(mq);
		mq = NULL;
		return;
	}

	if(msgctl(mq->fd, IPC_STAT, &mq->attr))
		goto fail;
	void *mi = (void *)&mq->info;
	if(msgctl(mq->fd, IPC_INFO, (struct msqid_ds *)(mi)))
		goto fail;
	mq->mbuf = (struct msgbuf *)malloc(sizeof(msgbuf) + mq->info.msgmax);
}

MessageQueue::MessageQueue(const char *name, size_t size, unsigned count)
{
	int tmp;

	mq = (ipc *)malloc(sizeof(ipc));
	mq->key = createipc(name, 'M');
	mq->creator = true;
	
	if(strrchr(name, '/') != name)
		goto fail;

remake:
	mq->fd = msgget(mq->key, IPC_CREAT | IPC_EXCL | 0660);

	if(mq->fd < 0 && errno == EEXIST) {
		tmp = msgget(mq->key, 0660);
		if(tmp > -1) {
			msgctl(tmp, IPC_RMID, NULL);
			goto remake;
		}
	}
	
    if(mq->fd == -1) {
fail:
        free(mq);
        mq = NULL;
		unlinkipc(name);
        return;
    }

	void *mi = (void *)&mq->info;
	if(msgctl(mq->fd, IPC_INFO, (struct msqid_ds *)mi)) {
		msgctl(mq->fd, IPC_RMID, NULL);
		goto fail;
	}

	if(msgctl(mq->fd, IPC_STAT, &mq->attr)) {
		msgctl(mq->fd, IPC_RMID, NULL);
		goto fail;
	}

	mq->attr.msg_qbytes = size * count;
	if(msgctl(mq->fd, IPC_SET, &mq->attr)) {
		msgctl(mq->fd, IPC_RMID, NULL);
		goto fail;
	}
	mq->mbuf = (struct msgbuf *)malloc(sizeof(struct msgbuf) + mq->info.msgmax);
}

MessageQueue::~MessageQueue()
{
	release();
}

void MessageQueue::release(void)
{
	if(mq) {
		free(mq->mbuf);
		if(mq->creator)
			msgctl(mq->fd, IPC_RMID, NULL);
		close(mq->fd);
		free(mq);
		mq = NULL;
	}
}

unsigned MessageQueue::getPending(void) const
{
	struct msqid_ds stat;

	if(!mq)
		return 0;

	if(msgctl(mq->fd, IPC_STAT, &stat))
		return 0;

	return stat.msg_qnum;
}

bool MessageQueue::puts(char *data)
{
	size_t len = string::count(data);

	if(len >= (size_t)(mq->info.msgmax))
		len = mq->info.msgmax - 1;

	return put(data, len);
}

bool MessageQueue::put(void *data, size_t len)
{
	if(!mq)
		return false;

	mq->mbuf->mtype = 1;

	if(len > (size_t)(mq->info.msgmax) || !len)
		len = mq->info.msgmax;

	memcpy(mq->mbuf->mtext, data, len);
	if(msgsnd(mq->fd, &mq->mbuf, len, IPC_NOWAIT) < 0)
		return false;
	return true;
}

bool MessageQueue::get(void *data, size_t len)
{
	if(!mq)
		return false;

	if(msgrcv(mq->fd, &mq->mbuf, len, 1, 0) < 0)
		return false;

	memcpy(data, mq->mbuf->mtext, len);
	return true;
}

bool MessageQueue::gets(char *data)
{
    if(!mq)
        return false;

    if(msgrcv(mq->fd, &mq->mbuf, mq->info.msgmax, 1, 0) < 0)
        return false;

	mq->mbuf->mtext[mq->info.msgmax - 1] = 0;
	strcpy(data, mq->mbuf->mtext);
    return true;
}


#endif

proc::proc(size_t ps, define *def) :
mempager(ps)
{
	root = NULL;

	while(def && def->name) {
		set(def->name, def->value);
		++def;
	}
}

proc::~proc()
{
	purge();
}

void proc::setenv(define *def)
{
	while(def && def->name) {
		setenv(def->name, def->value);
		++def;
	}
}

void proc::setenv(const char *id, const char *value)
{
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

	pthread_mutex_lock(&mutex);
#ifdef	HAVE_SETENV
	::setenv(id, value, 1);
#else
	char buf[128];
	snprintf(buf, sizeof(buf), "%s=%s", id, value);
	::putenv(buf);
#endif
	pthread_mutex_unlock(&mutex);
}

const char *proc::get(const char *id)
{
	member *key = find(id);
	if(!key)
		return NULL;

	return key->value;
}

#ifdef	_MSWINDOWS_
char **proc::getEnviron(void)
{
	char buf[1024 - 64];
	linked_pointer<member> env;
	unsigned idx = 0;
	unsigned count = LinkedObject::count(root) + 1;
	caddr_t mp = (caddr_t)alloc(count * sizeof(char *));
	char **envp = new(mp) char *[count];

	env = root;
	while(env) {
		snprintf(buf, sizeof(buf), "%s=%s", env->getId(), env->value);
		envp[idx++] = mempager::dup(buf);
	}
	envp[idx] = NULL;
	return envp;
}
#else

void proc::setEnviron(void)
{
	const char *id;

	linked_pointer<proc::member> env = begin();

	while(env) {
#ifdef	HAVE_SETENV
		::setenv(env->getId(), env->value, 1);
#else
		char buf[128];
		snprintf(buf, sizeof(buf), "%s=%s", env->getId(), env->value);
		putenv(buf);
#endif
		env.next();
	}

	if(getuid() == 0 && getppid() < 2)
		umask(002);

	id = get("UID");
	if(id && getuid() == 0) {
		if(getppid() > 1)
			umask(022);
		setuid(atoi(id));
	}

	id = get("HOME");
	if(id)
		chdir(id);

	id = get("PWD");
	if(id)
		chdir(id);	
}

pid_t proc::pidfile(const char *id, pid_t pid)
{
	char buf[128];
	pid_t opid;
	fd_t fd;

	snprintf(buf, sizeof(buf), "/var/run/%s", id);
	mkdir(buf, 0775);
	if(cpr_isdir(buf))
		snprintf(buf, sizeof(buf), "/var/run/%s/%s.pid", id, id);
	else
		snprintf(buf, sizeof(buf), "/tmp/%s.pid", id);

retry:
	fd = open(buf, O_CREAT|O_WRONLY|O_TRUNC|O_EXCL, 0755);
	if(fd < 0) {
		opid = pidfile(id);
		if(!opid || opid == 1 && pid > 1) {
			remove(buf);
			goto retry;
		}
		return opid;
	}

	if(pid > 1) {
		snprintf(buf, sizeof(buf), "%d\n", pid);
		write(fd, buf, strlen(buf));
	}
	close(fd);
	return 0;
}

pid_t proc::pidfile(const char *id)
{
	struct stat ino;
	time_t now;
	char buf[128];
	fd_t fd;
	pid_t pid;

	snprintf(buf, sizeof(buf), "/var/run/%s", id);
	if(cpr_isdir(buf))
		snprintf(buf, sizeof(buf), "/var/run/%s/%s.pid", id, id);
	else
		snprintf(buf, sizeof(buf), "/tmp/%s.pid", id);

	fd = open(buf, O_RDONLY);
	if(fd < 0 && errno == EPERM)
		return 1;

	if(fd < 0)
		return 0;

	if(read(fd, buf, 16) < 1) {
		goto bydate;
	}
	buf[16] = 0;
	pid = atoi(buf);
	if(pid == 1)
		goto bydate;

	close(fd);
	if(kill(pid, 0))
		return 0;

	return pid;

bydate:
	time(&now);
	fstat(fd, &ino);
	close(fd);
	if(ino.st_mtime + 30 < now)
		return 0;
	return 1;
}

bool proc::reload(const char *id)
{
	pid_t pid = pidfile(id);

	if(pid < 2)
		return false;

	kill(pid, SIGHUP);
	return true;
}

bool proc::shutdown(const char *id)
{
	pid_t pid = pidfile(id);

	if(pid < 2)
		return false;

	kill(pid, SIGINT);
	return true;
}

bool proc::terminate(const char *id)
{
	pid_t pid = pidfile(id);

	if(pid < 2)
		return false;

	kill(pid, SIGTERM);
	return true;
}

#endif

void proc::dup(const char *id, const char *value)
{
	member *env = find(id);

	if(!env) {
		caddr_t mp = (caddr_t)alloc(sizeof(member));
		env = new(mp) member(&root, mempager::dup(id));
	}
	env->value = mempager::dup(value);
};

void proc::set(char *id, const char *value)
{
    member *env = find(id);
	
	if(!env) {
	    caddr_t mp = (caddr_t)alloc(sizeof(member));
	    env = new(mp) member(&root, id);
	}

    env->value = value;
};

#ifdef _MSWINDOWS_

#else

int proc::spawn(const char *fn, char **args, int mode, pid_t *pid, fd_t *iov, proc *env)
{
	unsigned max = OPEN_MAX, idx = 0;
	int status;

	*pid = fork();

	if(*pid < 0)
		return -1;

	if(*pid) {
		closeiov(iov);
		switch(mode) {
		case SPAWN_DETACH:
		case SPAWN_NOWAIT:
			return 0;
		case SPAWN_WAIT:
			cpr_waitpid(*pid, &status);
			return status;
		}
	}

	while(iov && *iov > -1) {
		if(*iov != (fd_t)idx)
			dup2(*iov, idx);
		++iov;
		++idx;
	}

	while(idx < 3)
		++idx;
	
#if defined(HAVE_SYSCONF)
	max = sysconf(_SC_OPEN_MAX);
#endif
#if defined(HAVE_SYS_RESOURCE_H)
	struct rlimit rl;
	if(!getrlimit(RLIMIT_NOFILE, &rl))
		max = rl.rlim_cur;
#endif

	closelog();

	while(idx < max)
		close(idx++);

	if(mode == SPAWN_DETACH)
		cpr_pdetach();

	if(env)
		env->setEnviron();

	execvp(fn, args);
	exit(-1);
}

void proc::closeiov(fd_t *iov)
{
	unsigned idx = 0, np;

	while(iov && iov[idx] > -1) {
		if(iov[idx] != (fd_t)idx) {
			close(iov[idx]);
			np = idx;
			while(iov[++np] != -1) {
				if(iov[np] == iov[idx])
					iov[np] = (fd_t)np;
			}
		}
		++idx;
	}
}

void proc::createiov(fd_t *fd)
{
	fd[0] = 0;
	fd[1] = 1;
	fd[2] = 2;
	fd[3] = INVALID_HANDLE_VALUE;
}

void proc::attachiov(fd_t *fd, fd_t io)
{
	fd[0] = fd[1] = io;
	fd[2] = INVALID_HANDLE_VALUE;
}

void proc::detachiov(fd_t *fd)
{
	fd[0] = open("/dev/null", O_RDWR);
	fd[1] = fd[2] = fd[0];
	fd[3] = INVALID_HANDLE_VALUE;
}

#endif

fd_t proc::pipeInput(fd_t *fd, size_t size)
{
	fd_t pfd[2];

	if(!cpr_createpipe(pfd, size))
		return INVALID_HANDLE_VALUE;

	fd[0] = pfd[0];
	return pfd[1];
}

fd_t proc::pipeOutput(fd_t *fd, size_t size)
{
	fd_t pfd[2];

	if(!cpr_createpipe(pfd, size))
		return INVALID_HANDLE_VALUE;

	fd[1] = pfd[1];
	return pfd[0];
}

fd_t proc::pipeError(fd_t *fd, size_t size)
{
	return pipeOutput(++fd, size);
}	
