/*
  .c -- Binary patcher

  Copyright 2003,2004 Colin Percival

  For the terms under which this work may be distributed, please see
  the adjoining file "LICENSE".
*/

#ifndef BZIP2
#define BZIP2 "/usr/bin/bzip2"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

ssize_t loopread(int d,void *buf,size_t nbytes)
{
	ssize_t ptr,lenread;

	for(ptr=0;ptr<nbytes;ptr+=lenread) {
		lenread=read(d,buf+ptr,nbytes-ptr);
		if(lenread==0) return ptr;
		if(lenread==-1) return -1;
	};
	return ptr;
}

int bz2read(int fd,off_t offset,off_t len,char * fname,pid_t * pids)
{
	int p0[2],p1[2];
	u_char * data;

	if((pipe(p0)==-1) || (pipe(p1)==-1)) err(1,NULL);

	if((pids[0]=fork())==-1) err(1,NULL);
	if(pids[0]==0) {
		if(close(0) || close(1) || close(p0[0]) ||
			close(p1[0]) || close(p1[1])) err(1,NULL);
		if((data=malloc(len+1))==NULL) err(1,NULL);
		if((pread(fd,data,len,offset)!=len) || close(fd))
			err(1,"%s",fname);
		if((write(p0[1],data,len)!=len) || close(p0[1]))
			err(1,NULL);
		free(data);
		_exit(0);
	};

	if((pids[1]=fork())==-1) err(1,NULL);
	if(pids[1]==0) {
		if(close(0) || close(1) || close(p0[1]) ||
			close(p1[0])) err(1,NULL);
		if((dup2(p0[0],0)==-1) || close(p0[0])) err(1,NULL);
		if((dup2(p1[1],1)==-1) || close(p1[1])) err(1,NULL);
		if(close(fd)==-1) err(1,"%s",fname);

		execl(BZIP2,BZIP2,"-dc",NULL);
		err(1,"%s",BZIP2);
	};

	if(close(p0[0]) || close(p0[1]) || close(p1[1])) err(1,NULL);

	return p1[0];
}

off_t offtin(u_char *buf)
{
	off_t y;

	y=buf[7]&0x7F;
	y=y*256;y+=buf[6];
	y=y*256;y+=buf[5];
	y=y*256;y+=buf[4];
	y=y*256;y+=buf[3];
	y=y*256;y+=buf[2];
	y=y*256;y+=buf[1];
	y=y*256;y+=buf[0];

	if(buf[7]&0x80) y=-y;

	return y;
}

int main(int argc,char * argv[])
{
	int fd,ctrlpipe,diffpipe,extrapipe;
	pid_t pids[6];
	ssize_t patchsize,oldsize,newsize;
	ssize_t bzctrllen,bzdatalen;
	u_char header[32],buf[8];
	int version=0;
	u_char *old, *new;
	off_t oldpos,newpos;
	off_t ctrl[3];
	off_t lenread;
	off_t i;

	if(argc!=4) errx(1,"usage: %s oldfile newfile patchfile\n",argv[0]);

	if(((fd=open(argv[3],O_RDONLY,0))<0) ||
		((patchsize=lseek(fd,0,SEEK_END))==-1) ||
		(lseek(fd,0,SEEK_SET)!=0)) err(1,"%s",argv[3]);
	if(patchsize<32) errx(1,"Corrupt patch\n");

	/*
	  Ok, this is going to be messy.  There are two different patch
	formats which we need to support.

	  The old format (pre-4.0) is:
		0	8	"QSUFDIFF" or "BSDIFF30"
		8	8	X
		16	8	Y
		24	8	sizeof(newfile)
		32	X	bzip2(control block)
		32+X	Y	bzip2(data block)
	with control block a set of pairs (x,y) meaning "seek forward
	in oldfile by y bytes, and add the next x bytes to x bytes from
	the data block".

	  The new format (4.0) is:
		0	8	"BSDIFF40"
		8	8	X
		16	8	Y
		24	8	sizeof(newfile)
		32	X	bzip2(control block)
		32+X	Y	bzip2(diff block)
		32+X+Y	???	bzip2(extra block)
	with control block a set of triples (x,y,z) meaning "add x bytes
	from oldfile to x bytes from the diff block; copy y bytes from the
	extra block; seek forwards in oldfile by z bytes".
	*/

	if(read(fd,header,32)!=32) err(1,"%s",argv[3]);
	if(memcmp(header,"QSUFDIFF",8)==0) version=1;
	if(memcmp(header,"BSDIFF30",8)==0) version=1;
	if(memcmp(header,"BSDIFF40",8)==0) version=2;

	if(!version) errx(1,"Corrupt patch\n");

	bzctrllen=offtin(header+8);
	bzdatalen=offtin(header+16);
	newsize=offtin(header+24);
	if((bzctrllen<0) || (bzdatalen<0) || (newsize<0) ||
		((version==1) && (32+bzctrllen+bzdatalen!=patchsize)))
		errx(1,"Corrupt patch\n");

	ctrlpipe=bz2read(fd,32,bzctrllen,argv[3],pids);
	diffpipe=bz2read(fd,32+bzctrllen,bzdatalen,argv[3],pids+2);
	if(version==2) {
		extrapipe=bz2read(fd,32+bzctrllen+bzdatalen,
			patchsize-(32+bzctrllen+bzdatalen),argv[3],pids+4);
	};

	if(close(fd)==-1) err(1,"%s",argv[3]);
	if(((fd=open(argv[1],O_RDONLY,0))<0) ||
		((oldsize=lseek(fd,0,SEEK_END))==-1) ||
		((old=malloc(oldsize+1))==NULL) ||
		(lseek(fd,0,SEEK_SET)!=0) ||
		(read(fd,old,oldsize)!=oldsize) ||
		(close(fd)==-1)) err(1,"%s",argv[1]);
	if((new=malloc(newsize+1))==NULL) err(1,NULL);

	oldpos=0;newpos=0;
	while(newpos<newsize) {
		for(i=0;i<=version;i++) {
			if((lenread=loopread(ctrlpipe,buf,8))<0) err(1,NULL);
			if(lenread<8) errx(1,"Corrupt patch\n");
			ctrl[i]=offtin(buf);
		};

		if(version==1) oldpos+=ctrl[1];

		if(newpos+ctrl[0]>newsize) errx(1,"Corrupt patch\n");
		if((lenread=loopread(diffpipe,new+newpos,ctrl[0]))<0)
			err(1,NULL);
		if(lenread!=ctrl[0]) errx(1,"Corrupt patch\n");
		for(i=0;i<ctrl[0];i++)
			if((oldpos+i>=0) && (oldpos+i<oldsize))
				new[newpos+i]+=old[oldpos+i];
		newpos+=ctrl[0];
		oldpos+=ctrl[0];

		if(version==2) {
			if(newpos+ctrl[1]>newsize) errx(1,"Corrupt patch\n");
			if((lenread=loopread(extrapipe,new+newpos,ctrl[1]))<0)
				err(1,NULL);
			if(lenread!=ctrl[1]) errx(1,"Corrupt patch\n");

			newpos+=ctrl[1];
			oldpos+=ctrl[2];
		};
	};

	if(loopread(ctrlpipe,buf,1)!=0) errx(1,"Corrupt patch\n");
	if(loopread(diffpipe,buf,1)!=0) errx(1,"Corrupt patch\n");
	if(version==2)
		if(loopread(extrapipe,buf,1)!=0) errx(1,"Corrupt patch\n");

	if(close(ctrlpipe) || close(diffpipe) || 
		((version==2) && close(extrapipe)))
		err(1,NULL);
	for(i=0;i<(version+1)*2;i++) waitpid(pids[i],NULL,0);

	if(((fd=open(argv[2],O_CREAT|O_TRUNC|O_WRONLY,0666))<0) ||
		(write(fd,new,newsize)!=newsize) || (close(fd)==-1))
		err(1,"%s",argv[2]);

	free(new);
	free(old);

	return 0;
}
