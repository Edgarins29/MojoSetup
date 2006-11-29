#include "fileio.h"
#include "platform.h"

// !!! FIXME: don't have this here. (need unlink for now).
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>


typedef MojoArchive* (*MojoArchiveCreateEntryPoint)(MojoInput *io);

MojoArchive *MojoArchive_createZIP(MojoInput *io);

typedef struct
{
    const char *ext;
    MojoArchiveCreateEntryPoint create;
} MojoArchiveType;

static const MojoArchiveType archives[] =
{
    { "zip", MojoArchive_createZIP },
};

MojoArchive *MojoArchive_newFromInput(MojoInput *io, const char *origfname)
{
    int i;
    MojoArchive *retval = NULL;
    const char *ext = ((origfname != NULL) ? strrchr(origfname, '.') : NULL);
    if (ext != NULL)
    {
        // Try for an exact match.
        for (i = 0; i < STATICARRAYLEN(archives); i++)
        {
            if (strcasecmp(ext, archives[i].ext) == 0)
                return archives[i].create(io);
        } // for
    } // if

    // Try them all...
    for (i = 0; i < STATICARRAYLEN(archives); i++)
    {
        if ((retval = archives[i].create(io)) != NULL)
            return retval;
    } // for

    io->close(io);
    return NULL;  // nothing can handle this data.
} // MojoArchive_newFromInput


void MojoArchive_resetEntryInfo(MojoArchiveEntryInfo *info, int basetoo)
{
    char *base = info->basepath;
    free(info->filename);
    memset(info, '\0', sizeof (MojoArchiveEntryInfo));

    if (basetoo)
        free(base);
    else
        info->basepath = base;
} // MojoArchive_resetEntryInfo



boolean mojoInputToPhysicalFile(MojoInput *in, const char *fname)
{
    FILE *out = NULL;
    boolean iofailure = false;
    int32 br;

    STUBBED("mkdir first?");
    STUBBED("file permissions?");

    if (in == NULL)
        return false;

    STUBBED("fopen?");
    unlink(fname);
    out = fopen(fname, "wb");
    if (out == NULL)
        return false;

    while (!iofailure)
    {
        br = (int32) in->read(in, scratchbuf_128k, sizeof (scratchbuf_128k));
        if (br == 0)  // we're done!
            break;
        else if (br < 0)
            iofailure = true;
        else
        {
            if (fwrite(scratchbuf_128k, br, 1, out) != 1)
                iofailure = true;
        } // else
    } // while

    fclose(out);
    if (iofailure)
    {
        unlink(fname);
        return false;
    } // if

    return true;
} // mojoInputToPhysicalFile



// MojoInputs from files on the OS filesystem.

typedef struct
{
    FILE *handle;
    char *path;
} MojoInputFileInstance;

static int64 MojoInput_file_read(MojoInput *io, void *buf, uint32 bufsize)
{
    MojoInputFileInstance *inst = (MojoInputFileInstance *) io->opaque;
    return (int64) fread(buf, 1, bufsize, inst->handle);
} // MojoInput_file_read

static boolean MojoInput_file_seek(MojoInput *io, uint64 pos)
{
    MojoInputFileInstance *inst = (MojoInputFileInstance *) io->opaque;
#if 1
    rewind(inst->handle);
    while (pos)
    {
        // do in a loop to make sure we seek correctly in > 2 gig files.
        if (fseek(inst->handle, (long) (pos & 0x7FFFFFFF), SEEK_CUR) == -1)
            return false;
        pos -= (pos & 0x7FFFFFFF);
    } // while
    return true;
#else
    return (fseeko(inst->handle, pos, SEEK_SET) == 0);
#endif
} // MojoInput_file_seek

static int64 MojoInput_file_tell(MojoInput *io)
{
    MojoInputFileInstance *inst = (MojoInputFileInstance *) io->opaque;
//    return (int64) ftello(inst->handle);
    STUBBED("ftell is 32 bit!\n");
    return (int64) ftell(inst->handle);
} // MojoInput_file_tell

static int64 MojoInput_file_length(MojoInput *io)
{
    MojoInputFileInstance *inst = (MojoInputFileInstance *) io->opaque;
    int fd = fileno(inst->handle);
    struct stat statbuf;
    if ((fd == -1) || (fstat(fd, &statbuf) == -1))
        return -1;
    return((int64) statbuf.st_size);
} // MojoInput_file_length

static MojoInput *MojoInput_file_duplicate(MojoInput *io)
{
    MojoInputFileInstance *inst = (MojoInputFileInstance *) io->opaque;
    return MojoInput_newFromFile(inst->path);
} // MojoInput_file_duplicate

static void MojoInput_file_close(MojoInput *io)
{
    MojoInputFileInstance *inst = (MojoInputFileInstance *) io->opaque;
    fclose(inst->handle);
    free(inst->path);
    free(inst);
    free(io);
} // MojoInput_file_close

MojoInput *MojoInput_newFromFile(const char *fname)
{
    char path[PATH_MAX];
    MojoInputFileInstance *inst;

    if (realpath(fname, path) == NULL)
        strcpy(path, fname);  // can this actually happen and fopen work?

    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return NULL;

    inst = (MojoInputFileInstance *) xmalloc(sizeof (MojoInputFileInstance));
    inst->path = xstrdup(path);
    inst->handle = f;

    MojoInput *io = (MojoInput *) xmalloc(sizeof (MojoInput));
    io->read = MojoInput_file_read;
    io->seek = MojoInput_file_seek;
    io->tell = MojoInput_file_tell;
    io->length = MojoInput_file_length;
    io->duplicate = MojoInput_file_duplicate;
    io->close = MojoInput_file_close;
    io->opaque = inst;
    return io;
} // MojoInput_newFromFile



// MojoInputs from blocks of memory.

typedef struct
{
    uint8 *mem;
    uint32 bytes;
    uint32 pos;
} MojoInputMemInstance;

static int64 MojoInput_memory_read(MojoInput *io, void *buf, uint32 bufsize)
{
    MojoInputMemInstance *inst = (MojoInputMemInstance *) io->opaque;
    uint32 left = inst->bytes - inst->pos;
    if (bufsize > left)
        bufsize = left;

    if (bufsize)
    {
        memcpy(buf, inst->mem + inst->pos, bufsize);
        inst->pos += bufsize;
    } // if
    return bufsize;
} // MojoInput_memory_read

static boolean MojoInput_memory_seek(MojoInput *io, uint64 pos)
{
    MojoInputMemInstance *inst = (MojoInputMemInstance *) io->opaque;
    if (pos > (inst->bytes))
        return false;
    inst->pos = pos;
    return true;
} // MojoInput_memory_seek

static int64 MojoInput_memory_tell(MojoInput *io)
{
    MojoInputMemInstance *inst = (MojoInputMemInstance *) io->opaque;
    return inst->pos;
} // MojoInput_memory_tell

static int64 MojoInput_memory_length(MojoInput *io)
{
    MojoInputMemInstance *inst = (MojoInputMemInstance *) io->opaque;
    return(inst->bytes);
} // MojoInput_memory_length

static MojoInput *MojoInput_memory_duplicate(MojoInput *io)
{
    MojoInputMemInstance *inst = (MojoInputMemInstance *) io->opaque;
    return MojoInput_newFromMemory(inst->mem, inst->bytes);
} // MojoInput_memory_duplicate

static void MojoInput_memory_close(MojoInput *io)
{
    free(io->opaque);
    free(io);
} // MojoInput_memory_close

MojoInput *MojoInput_newFromMemory(void *mem, uint32 bytes)
{
    MojoInputMemInstance *inst;
    inst = (MojoInputMemInstance *) xmalloc(sizeof (MojoInputMemInstance));
    inst->mem = mem;
    inst->bytes = bytes;

    MojoInput *io = (MojoInput *) xmalloc(sizeof (MojoInput));
    io->read = MojoInput_memory_read;
    io->seek = MojoInput_memory_seek;
    io->tell = MojoInput_memory_tell;
    io->length = MojoInput_memory_length;
    io->duplicate = MojoInput_memory_duplicate;
    io->close = MojoInput_memory_close;
    io->opaque = inst;
    return io;
} // MojoInput_newFromMemory


// MojoArchives from directories on the OS filesystem.

// !!! FIXME: abstract the unixy bits into the platform/ dir.
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <dirent.h>

typedef struct
{
    DIR *dir;
    char *base;
} MojoArchiveDirInstance;

static boolean MojoArchive_dir_enumerate(MojoArchive *ar, const char *path)
{
    char *fullpath = NULL;
    MojoArchiveDirInstance *inst = (MojoArchiveDirInstance *) ar->opaque;
    if (inst->dir != NULL)
        closedir(inst->dir);

    MojoArchive_resetEntryInfo(&ar->prevEnum, 1);
    fullpath = (char *) alloca(strlen(inst->base) + strlen(path) + 2);
    sprintf(fullpath, "%s/%s", inst->base, path);
    ar->prevEnum.basepath = xstrdup(path);
    inst->dir = opendir(fullpath);
    return (inst->dir != NULL);
} // MojoArchive_dir_enumerate


static const MojoArchiveEntryInfo *MojoArchive_dir_enumNext(MojoArchive *ar)
{
    struct stat statbuf;
    char *fullpath = NULL;
    struct dirent *dent;
    MojoArchiveDirInstance *inst = (MojoArchiveDirInstance *) ar->opaque;
    if (inst->dir == NULL)
        return NULL;

    MojoArchive_resetEntryInfo(&ar->prevEnum, 0);

    dent = readdir(inst->dir);
    if (dent == NULL)  // end of dir?
    {
        closedir(inst->dir);
        inst->dir = NULL;
        return NULL;
    } // if

    if ((strcmp(dent->d_name, ".") == 0) || (strcmp(dent->d_name, "..") == 0))
        return MojoArchive_dir_enumNext(ar);

    ar->prevEnum.filename = xstrdup(dent->d_name);
    fullpath = (char *) alloca(strlen(inst->base) +
                               strlen(ar->prevEnum.basepath) +
                               strlen(ar->prevEnum.filename) + 3);

    sprintf(fullpath, "%s/%s/%s",
                inst->base,
                ar->prevEnum.basepath,
                ar->prevEnum.filename);

    if (stat(fullpath, &statbuf) != -1)
    {
        ar->prevEnum.filesize = statbuf.st_size;
        if (S_ISDIR(statbuf.st_mode))
            ar->prevEnum.type = MOJOARCHIVE_ENTRY_DIR;
        else if (S_ISREG(statbuf.st_mode))
            ar->prevEnum.type = MOJOARCHIVE_ENTRY_FILE;
        else if (S_ISLNK(statbuf.st_mode))
            ar->prevEnum.type = MOJOARCHIVE_ENTRY_SYMLINK;
    } // if

    return &ar->prevEnum;
} // MojoArchive_dir_enumNext


static MojoInput *MojoArchive_dir_openCurrentEntry(MojoArchive *ar)
{
    MojoArchiveDirInstance *inst = (MojoArchiveDirInstance *) ar->opaque;
    char *fullpath = (char *) alloca(strlen(inst->base) +
                                     strlen(ar->prevEnum.basepath) +
                                     strlen(ar->prevEnum.filename) + 3);
    sprintf(fullpath, "%s/%s/%s",
                inst->base,
                ar->prevEnum.basepath,
                ar->prevEnum.filename);

    return MojoInput_newFromFile(fullpath);
} // MojoArchive_dir_openCurrentEntry


static void MojoArchive_dir_close(MojoArchive *ar)
{
    MojoArchiveDirInstance *inst = (MojoArchiveDirInstance *) ar->opaque;
    if (inst->dir != NULL)
        closedir(inst->dir);
    free(inst->base);
    free(inst);
    MojoArchive_resetEntryInfo(&ar->prevEnum, 1);
    free(ar);
} // MojoArchive_dir_close


MojoArchive *MojoArchive_newFromDirectory(const char *dirname)
{
    char resolved[PATH_MAX];
    MojoArchiveDirInstance *inst;
    if (realpath(dirname, resolved) == NULL)
        return NULL;

    inst = (MojoArchiveDirInstance *) xmalloc(sizeof (MojoArchiveDirInstance));
    inst->base = xstrdup(resolved);
    MojoArchive *ar = (MojoArchive *) xmalloc(sizeof (MojoArchive));
    ar->enumerate = MojoArchive_dir_enumerate;
    ar->enumNext = MojoArchive_dir_enumNext;
    ar->openCurrentEntry = MojoArchive_dir_openCurrentEntry;
    ar->close = MojoArchive_dir_close;
    ar->opaque = inst;
    return ar;
} // MojoArchive_newFromDirectory




MojoArchive *GBaseArchive = NULL;

MojoArchive *MojoArchive_initBaseArchive(void)
{
    if (GBaseArchive != NULL)
        return GBaseArchive;
    else
    {
        const char *basepath = MojoPlatform_appBinaryPath();
        MojoInput *io = MojoInput_newFromFile(basepath);

        STUBBED("chdir to path of binary");

        if (io != NULL)
            GBaseArchive = MojoArchive_newFromInput(io, basepath);

        if (GBaseArchive == NULL)
            GBaseArchive = MojoArchive_newFromDirectory(".");
    } // else

    return GBaseArchive;
} // MojoArchive_initBaseArchive


void MojoArchive_deinitBaseArchive(void)
{
    if (GBaseArchive != NULL)
    {
        GBaseArchive->close(GBaseArchive);
        GBaseArchive = NULL;
    } // if
} // MojoArchive_deinitBaseArchive

// end of fileio.c ...
