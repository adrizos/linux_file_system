

/*
	Alex Drizos
	cs1550
	Project 4 - Custom File System



	FUSE: Filesystem in Userspace
	Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
	This program can be distributed under the terms of the GNU GPL.
	See the file COPYING.
	gcc -Wall `pkg-config fuse --cflags --libs` cs1550.c -o cs1550
*/

#define	FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

//size of a disk block
#define	BLOCK_SIZE 512

//we'll use 8.3 filenames
#define	MAX_FILENAME 8
#define	MAX_EXTENSION 3

//How many files can there be in one directory?
#define	MAX_FILES_IN_DIR (BLOCK_SIZE - (MAX_FILENAME + 1) - sizeof(int)) / ((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))

//How much data can one block hold?
#define	MAX_DATA_IN_BLOCK (BLOCK_SIZE - sizeof(size_t) - sizeof(long))

struct cs1550_directory_entry
{

	int nFiles;						//How many files are in this directory.
									//Needs to be less than MAX_FILES_IN_DIR
	char dname[MAX_FILENAME	+ 1];	//the directory name (plus space for a nul)
	struct cs1550_file_directory
	{
		char fname[MAX_FILENAME + 1];	//filename (plus space for nul)
		char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
		size_t fsize;			//file size
		long nStartBlock;		//where the first block is on disk
	} __attribute__((packed)) files[MAX_FILES_IN_DIR];		//There is an array of these
	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.
	char padding[BLOCK_SIZE - MAX_FILES_IN_DIR * sizeof(struct cs1550_file_directory) - sizeof(int)];
};

typedef struct cs1550_directory_entry cs1550_directory_entry;

#define MAX_DIRS_IN_ROOT (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + sizeof(long))

struct cs1550_disk_block
{
	//Two choices for interpreting size:
	//	1) how many bytes are being used in this block
	//	2) how many bytes are left in the file
	//Either way, size tells us if we need to chase the pointer to the next
	//disk block. Use it however you want.
	size_t size;

	//The next disk block, if needed. This is the next pointer in the linked
	//allocation list
	long nNextBlock;

	//And all the rest of the space in the block can be used for actual data
	//storage.
	char data[MAX_DATA_IN_BLOCK];
};

typedef struct cs1550_disk_block cs1550_disk_block;

/*
 Update the directory in the .directories file after adding a file
*/
static int updateDirectory(cs1550_directory_entry *directory, int index)
{
	if (directory == NULL) {
		return -1;
	}

	else {
		FILE *file = fopen(".directories", "rb+");
		fseek(file, sizeof(cs1550_directory_entry) * index, SEEK_SET);
		fwrite(directory, sizeof(cs1550_directory_entry),1,file);
		fclose(file); //close file
		return 0;
	}
}


static int getDirectory(cs1550_directory_entry *found_dir, int index)
{
	//sets the directory info but returns 0 on errors
	int returnValueVar = 0;
	int seek;
	int read;
	FILE *file = fopen(".directories", "rb");
	if (file == NULL) {
		return returnValueVar;
	}

	seek = fseek(file, sizeof(cs1550_directory_entry) * index, SEEK_SET);
	if (seek == -1) {
		return returnValueVar;
	}

	read = fread(found_dir, sizeof(cs1550_directory_entry), 1, file);
	if (read == 1) {
		returnValueVar = 1;
	}

	fclose(file); //close
	return returnValueVar;
}

static int findDirectory(char *dirName)
{
	// Seek through the .directories file, return dir index
	//set return value here and return error code if not found below
	int returnValueVar = -1;
	cs1550_directory_entry tempDir;
	int index = 0;
	while ((getDirectory(&tempDir, index) != 0) && (returnValueVar == -1)) {
		if (strcmp(dirName, tempDir.dname) == 0) {
			returnValueVar = index;
		}
		//increment
		index++;
	} //end of while

	return returnValueVar;
}

/*
Returns the index of the file in the .disk with "filename"
or will return -1 if it does not exist.
*/
static int findFile(cs1550_directory_entry *directory, char * filename, char *extension)
{
	int i;
	for (i = 0; i < directory->nFiles; i++)	{
		// checks for same filename
		if ((strcmp(filename, directory->files[i].fname) == 0) && (strlen(filename) == strlen(directory->files[i].fname))) 	{
			if ((extension[0] == '\0') && (directory->files[i].fext[0] == '\0')) {
				return i; //return current index
			}//if extension is NULL

			else if ((extension != NULL) && (strcmp(extension, directory->files[i].fext) == 0)) {
				return i;
			}//extension !null and matches

		}
	}

	return -1;
}

/*
 * Allocates a new block for creating a file
 * returning it's position in .disk on success
 */
static int allocateBlock()
{
	int indexOfBlock = -1;
	int newIndex;
	FILE *file = fopen(".disk", "rb+");
	if (file == NULL) {
		return -1; //return error code
	} //should .disk not exist


	int seekReturn = fseek(file, -sizeof(cs1550_disk_block), SEEK_END);
	if (seekReturn == -1) {
		return -1; //return error
	}

	int readReturn = fread(&indexOfBlock, sizeof(int), 1, file);
	if (readReturn == -1) {
		return -1;
	}

	seekReturn = fseek(file, -sizeof(cs1550_disk_block), SEEK_END);
	if (seekReturn == -1) {
		return -1;
	}

	newIndex = indexOfBlock + 1;
	int writeReturn = fwrite(&newIndex, 1, sizeof(int), file);
	if (writeReturn == -1) {
		return -1;
	}

	fclose(file); //closes file
	return indexOfBlock;
}

/*
 *read a block from disk and load it to memory
 */
static int readBlock(cs1550_disk_block *returnBlock, int position)
{
	int returnValVar = -1;
	FILE *file = fopen(".disk", "rb");
	if (file == NULL) {
		return returnValVar;
	}

	int seekReturn = fseek(file, sizeof(cs1550_disk_block) * position, SEEK_SET);
	if (seekReturn == -1) {
		return returnValVar;
	}

	int readReturn = fread(returnBlock, sizeof(cs1550_disk_block), 1, file);
	if (readReturn == 1) {
		returnValVar = 0;
	}

	fclose(file); //closes file
	return returnValVar;
}

/*
 *write a block to disk from memory
 */
static int writeBlock(cs1550_disk_block *returnBlock, int position)
{
	int returnValVar = -1;
	FILE *file = fopen(".disk", "rb+");
	if (file == NULL) {
		return returnValVar;
	}

	int seekReturn = fseek(file, sizeof(cs1550_disk_block) * position, SEEK_SET);
	if (seekReturn == -1) {
		return returnValVar;
	}

	int writeReturn = fwrite(returnBlock, sizeof(cs1550_disk_block), 1, file);
	if (writeReturn == 1) {
		returnValVar = 0;
	}

	fclose(file); //closes file
	return returnValVar;
}


/*
 * Transfers data from a buffer to the data section of a block
 *
 * Returns:
 * int representing the amount of data left to transfer
 */
static int buffer_to_block(cs1550_disk_block *block, const char *buffer, int position, int dataLeft)
{
	while(dataLeft > 0) {
		//not enough here
		if (position > MAX_DATA_IN_BLOCK) {
			return dataLeft; //returns remaining data to write
		}

		else {
			block->data[position] = *buffer;
			//incrememnt pointer to buffer
			buffer++;
			dataLeft--; //decrement data left to write
			//check
			if (block->size <= position) {
					block->size += 1;
			}

			position++;
		}
	}
	return dataLeft;
}


/*
 * Transfers data from the data section of a block to a buffer
 *
 * Returns:
 * integer representing the amount of data left to transfer
 * -1 if too much data was asked for
 */
static int block_to_buffer(cs1550_disk_block *block, char *buffer, int position, int dataLeft)
{
	//while theres data left
	while (dataLeft > 0) {

		//check data position for read
		if (position > MAX_DATA_IN_BLOCK) {
			return dataLeft;
		}

		else {
			*buffer = block->data[position];
			//book keeping
			buffer++;
			dataLeft--;
			position++;
		}
	}
	return dataLeft;
}

/*
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not.
 *
 * man -s 2 stat will show the fields of a stat structure
 */
static int cs1550_getattr(const char *path, struct stat *stbuf)
{
	cs1550_directory_entry currentDirectory;
	char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1], extension[MAX_EXTENSION + 1];
	int returnValueVar = -ENOENT;
	int indexOfDir, i;

	memset(stbuf, 0, sizeof(struct stat));
	//is path the root dir?
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		returnValueVar = 0; // sets return value to zero if no errors occur
	}

	else
	{
		memset(directory, 0, MAX_FILENAME + 1);
		memset(filename, 0, MAX_FILENAME + 1);
		memset(extension, 0, MAX_EXTENSION + 1);

		sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
		indexOfDir = findDirectory(directory);

		if(indexOfDir != -1) //if it is a valid directory
		{
			getDirectory(&currentDirectory,indexOfDir);

			if(directory != NULL && (filename[0] == '\0')) //not a file
			{
					stbuf->st_mode = S_IFDIR | 0755;
					stbuf->st_nlink = 2;
					returnValueVar = 0; //no error
			}
			else
			{
				for (i = 0; i < currentDirectory.nFiles; i++)
				{
					if(strcmp(currentDirectory.files[i].fname,filename) == 0) //filename matches
					{
						if (strcmp(currentDirectory.files[i].fext,extension) == 0) //extension matches
						{
							stbuf->st_mode = S_IFREG | 0666;
							stbuf->st_nlink = 1;
							stbuf->st_size = currentDirectory.files[i].fsize;
							returnValueVar = 0; //no error
							break;
						}
					}
				}
			}
		}
		else
			returnValueVar = -ENOENT; // directory does not exist
	}
	return returnValueVar;
}

/*
 * Called whenever the contents of a directory are desired. Could be from an 'ls'
 * or could even be when a user hits TAB to do autocompletion
 */
static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	//Since we're building with -Wall (all warnings reported) we need
	//to "use" every parameter, so let's just cast them to void to
	//satisfy the compiler
	(void) offset;
	(void) fi;
	int returnValueVar = 0;
	int indexOfDir;
	int i = 0; //variable for loops
	char charBuff[50];
	cs1550_directory_entry currentDirectory;

	char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1], extension[MAX_EXTENSION + 1];
	memset(directory, 0, MAX_FILENAME + 1);
	memset(filename, 0, MAX_FILENAME + 1);
	memset(extension, 0, MAX_EXTENSION + 1);

	//test if its the root
	if (strcmp(path, "/") != 0) {
		sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
		indexOfDir = findDirectory(directory);
		if ((directory != NULL) && (indexOfDir != -1)) {
			getDirectory(&currentDirectory, indexOfDir);
			//the filler function allows us to add entries to the listing
			//read the fuse.h file for a description (in the ../include dir)
			filler(buf, ".", NULL, 0);
			filler(buf, "..", NULL, 0);

			for (i = 0; i < currentDirectory.nFiles; i++)
			{
				if (strlen(currentDirectory.files[i].fext) > 0){
					sprintf(charBuff, "%s.%s", currentDirectory.files[i].fname, currentDirectory.files[i].fext);
				}

				else {
					sprintf(charBuff, "%s", currentDirectory.files[i].fname);
				}


				filler(buf, charBuff, NULL, 0);
			}

			returnValueVar = 0;
		}
		else {
			returnValueVar = -ENOENT;
		}

	}
	else
	{
		i = 0; //reset loop counter variable
		filler(buf, ".", NULL,0);
		filler(buf, "..", NULL, 0);
		while (getDirectory(&currentDirectory, i) != 0) {
			filler(buf, currentDirectory.dname, NULL, 0);
			i++; //increment i
		}
		returnValueVar = 0;
	}

	return 0;
}

/*
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int cs1550_mkdir(const char *path, mode_t mode)
{
	(void) mode;
	char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1], extension[MAX_EXTENSION + 1];
	int returnValueVar = 0;
	cs1550_directory_entry tDir; //temporary directory varriable

	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	// no directory specification
	if (directory == NULL || directory[0] == '\0') {
		returnValueVar = -EPERM;
	}

	else {
		//check if directory exists
		if (findDirectory(directory) == -1) {

			//check if file's name is in bounds
			if (strlen(directory) <= MAX_FILENAME) {
				memset(&tDir, 0, sizeof(struct cs1550_directory_entry));
				strcpy(tDir.dname, directory);
				tDir.nFiles = 0;

				FILE *f = fopen(".directories", "ab");
				fwrite(&tDir, sizeof(cs1550_directory_entry),1,f);
				fclose(f);
			}
			else {
				//files name is not in bounds
				returnValueVar = -ENAMETOOLONG;
			}

		}

		else {
			//directory has been created already
			returnValueVar = -EEXIST;
		}

	}

	return returnValueVar;
}

/*
 * Removes a directory.
 */
static int cs1550_rmdir(const char *path)
{
	(void) path;
    return 0;
}

/*
 * Does the actual creation of a file. Mode and dev can be ignored.
 *
 */
static int cs1550_mknod(const char *path, mode_t mode, dev_t dev)
{
	(void) mode;
	(void) dev;
	char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1], extension[MAX_EXTENSION + 1];
	int ret = 0;
	memset(directory, 0, MAX_FILENAME + 1);
	memset(filename, 0, MAX_FILENAME + 1);
	memset(extension, 0, MAX_EXTENSION + 1);
	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	//don't create files in root
	if (directory != NULL) {

		int dirIndex = findDirectory(directory);

		if (strlen(filename) > MAX_FILENAME || strlen(extension) > MAX_EXTENSION) {
				ret = -ENAMETOOLONG;
		}

		else {

			cs1550_directory_entry dirEntry;
			getDirectory(&dirEntry, dirIndex);
			//if file isn't found
			if (findFile(&dirEntry, filename, extension) == -1)	{

				strcpy(dirEntry.files[dirEntry.nFiles].fname ,filename);
				if (strlen(extension) > 0) {
					strcpy(dirEntry.files[dirEntry.nFiles].fext ,extension);
				}

				else {
					strcpy(dirEntry.files[dirEntry.nFiles].fext, "\0");
				}

				dirEntry.files[dirEntry.nFiles].nStartBlock = allocateBlock();
				dirEntry.files[dirEntry.nFiles].fsize = 0;
				dirEntry.nFiles = dirEntry.nFiles+1;
				updateDirectory(&dirEntry, dirIndex);
				ret = 0;
			}

			else {
				ret = -EEXIST;
			}
		}
	}

	else {
		ret = -EPERM;
	}

    return ret;
}

/*
 * Deletes a file
 */
static int cs1550_unlink(const char *path)
{
    (void) path;

    return 0;
}

/*
 * Read size bytes from file into buf starting from offset
 *
 */
static int cs1550_read(const char *path, char *buf, size_t size, off_t offset,struct fuse_file_info *fi) {

	(void) fi;
	//create copy
	int tOffset = offset;
	cs1550_directory_entry tempDir;
	cs1550_disk_block tempBlock;
	int retValVar = 0;

	char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1], extension[MAX_EXTENSION + 1];
	memset(directory, 0, MAX_FILENAME + 1);
	memset(filename, 0, MAX_FILENAME + 1);
	memset(extension, 0, MAX_EXTENSION + 1);
	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	if (offset > size || size <= 0) {
		return -1;
	}


	if (directory != NULL) {

		if (filename == NULL) {
			return -EISDIR;
		}


		if (strlen(filename) < MAX_FILENAME) {

			if ((extension == NULL) || (extension[0] == '\0') || ((extension != NULL && extension[0] != '\0') && (strlen(extension) <= MAX_EXTENSION)))	{
				//ext could be null or !null and < the len max
				int dirIndex = findDirectory(directory);
				if (dirIndex == -1) {
					retValVar = -1;
				}

				int result = getDirectory(&tempDir, dirIndex);
				if (result == 0) {
					retValVar = -1; // if error
				}

				int fileIndex = findFile(&tempDir, filename, extension);
				//file exists
				if (fileIndex != -1) {
					//when file is empty
					if (tempDir.files[fileIndex].fsize == 0) {
						return 0;
					}

					int blockNum = tempDir.files[fileIndex].nStartBlock;

					while (offset < size) {

						readBlock(&tempBlock, blockNum);
						if (tOffset > MAX_DATA_IN_BLOCK) {

							blockNum = tempBlock.nNextBlock;
							tOffset -= MAX_DATA_IN_BLOCK;
							continue; //exit loop
						}

						else {
							int bufferReturn = block_to_buffer(&tempBlock, buf, tOffset, size - offset);
							tOffset = 0;
							if (bufferReturn == 0) {
								break; //exit
							}

							else {
								blockNum = tempBlock.nNextBlock;
								offset += MAX_DATA_IN_BLOCK;
								buf += MAX_DATA_IN_BLOCK;
							}
						}
					}

					retValVar = size;
				}
				else {
					// return error file exists already
					retValVar = -1;
				}

			}
			else {
				//return error extension too long
				retValVar = -1;
			}

		}
		else {
			//return error filename too long
			retValVar = -1;
		}

	}
	else {
		//return error
		retValVar = -1;
	}

	//otherwise return here
	return retValVar;
}

/*
 * Write size bytes from buf into file starting from offset
 *
 */
static int cs1550_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	(void) fi;
	//temp offset var
	int tOffset = offset;
	int retValVar = 0;

	char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1], extension[MAX_EXTENSION + 1];
	memset(directory, 0, MAX_FILENAME + 1);
	memset(filename, 0, MAX_FILENAME + 1);
	memset(extension, 0, MAX_EXTENSION + 1);
	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	cs1550_directory_entry tempDir;
	cs1550_disk_block tempBlock;
	//clear the temporary disk block here
	memset(&tempBlock, 0, sizeof(cs1550_disk_block));

	// handles writing nothing or writing beyond the file's end
	if ((offset > size) || (size <= 0)) {
		//return error value
		return -1;
	}


	if (directory != NULL) {

		if ((filename != NULL) && (filename[0] != '\0') && (strlen(filename) < MAX_FILENAME)) {

			if ((extension == NULL) || (extension[0] == '\0') || ((extension != NULL && extension[0] != '\0') && (strlen(extension) <= MAX_EXTENSION)))	{

				//ext could be null or !null and < the len max
				int dirIndex = findDirectory(directory);
				if (dirIndex == -1) {
					// if return value is an error code - file not found
					retValVar = -1;
				}


				if (getDirectory(&tempDir, dirIndex) == 0) {
					// error value trying to get curr dir
					retValVar = -1;
				}

				//check
				int fileIndex = findFile(&tempDir, filename, extension);
				//if return value is not error code
				if (fileIndex != -1) {
					int blockNum = tempDir.files[fileIndex].nStartBlock;
					tempDir.files[fileIndex].fsize = size;
					//update directories file
					updateDirectory(&tempDir, dirIndex);

					while (tOffset >= MAX_DATA_IN_BLOCK) {
						blockNum = tempBlock.nNextBlock;
						tOffset -= MAX_DATA_IN_BLOCK;
						readBlock(&tempBlock, blockNum);
					} //end of while loop

					while (offset < size) {
						if (tOffset > MAX_DATA_IN_BLOCK) {
							blockNum = tempBlock.nNextBlock;
							tOffset -= MAX_DATA_IN_BLOCK;
							continue;
						}

						else {
							int bufferReturn = buffer_to_block(&tempBlock, buf, tOffset, size - offset);
							if (bufferReturn != 0 && (tempBlock.nNextBlock <= 0)) {
								//need aditional block
								tempBlock.nNextBlock = allocateBlock();
							}

							writeBlock(&tempBlock, blockNum);
							tOffset = 0;

							if (bufferReturn == 0) {
								break; //break out of while loop
							}

							else {
								blockNum = tempBlock.nNextBlock;
								offset += MAX_DATA_IN_BLOCK;
								readBlock(&tempBlock, blockNum);
								buf += MAX_DATA_IN_BLOCK;
							}
						}
					}

					retValVar = size;
				}
				else {
					//file there
					retValVar = -1;
				}

			}
			else {
				//return error extension too long
				retValVar = -1;
			}

		}
		else {
			//return error file not there
			retValVar = -EISDIR;
		}

	}
	else {
		retValVar = -1;
	}

	//otherwise return this
	return retValVar;
}

/******************************************************************************
 *
 *  DO NOT MODIFY ANYTHING BELOW THIS LINE
 *
 *****************************************************************************/

/*
 * truncate is called when a new file is created (with a 0 size) or when an
 * existing file is made shorter. We're not handling deleting files or
 * truncating existing ones, so all we need to do here is to initialize
 * the appropriate directory entry.
 *
 */
static int cs1550_truncate(const char *path, off_t size)
{
	(void) path;
	(void) size;

    return 0;
}


/*
 * Called when we open a file
 *
 */
static int cs1550_open(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;
    /*
        //if we can't find the desired file, return an error
        return -ENOENT;
    */

    //It's not really necessary for this project to anything in open

    /* We're not going to worry about permissions for this project, but
	   if we were and we don't have them to the file we should return an error
        return -EACCES;
    */

    return 0; //success!
}

/*
 * Called when close is called on a file descriptor, but because it might
 * have been dup'ed, this isn't a guarantee we won't ever need the file
 * again. For us, return success simply to avoid the unimplemented error
 * in the debug log.
 */
static int cs1550_flush (const char *path , struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;

	return 0; //success!
}


//register our new functions as the implementations of the syscalls
static struct fuse_operations hello_oper = {
    .getattr	= cs1550_getattr,
    .readdir	= cs1550_readdir,
    .mkdir	= cs1550_mkdir,
	.rmdir = cs1550_rmdir,
    .read	= cs1550_read,
    .write	= cs1550_write,
	.mknod	= cs1550_mknod,
	.unlink = cs1550_unlink,
	.truncate = cs1550_truncate,
	.flush = cs1550_flush,
	.open	= cs1550_open,
};

//Don't change this.
int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &hello_oper, NULL);
}
