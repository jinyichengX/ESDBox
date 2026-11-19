/*
 * MIT License
 *
 * Copyright (c) 2025 JinYiCheng
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* This module only adapted to 
 * elFAT, Windows and Linux
 */

#include "ipgui_vfs.h"
#include "ipgui_memory.h"
#include "ipgui_debug.h"

#define IPGUI_FS_MEM_ALLOC(size)    ipgui_mem_alloc(ipgui_smem, (size))
#define IPGUI_FS_MEM_FREE(p)        ipgui_mem_free(ipgui_smem, (void *)(p))

__IPGUI_API__ ipgui_err_t ipgui_file_init(ipgui_file_t * file)
{
    if (!file)
        return IPGUI_ERR_PARAM;

    file->pri_file = (void *)0;
    file->rd_cache = (ipgui_file_cache_t *)0;
    file->wr_cache = (ipgui_file_cache_t *)0;
    file->fs       = (ipgui_fs_ops_t *)0;
    
    return IPGUI_ERR_OK;
}

__IPGUI_API__ ipgui_err_t __ipgui_link_fs(ipgui_file_t * file, ipgui_fs_ops_t * fs)
{
    if (!file || !fs)
        return IPGUI_ERR_PARAM;

    file->fs = fs;
    return IPGUI_ERR_OK;
}

__IPGUI_API__ ipgui_err_t ipgui_link_fs_auto(ipgui_file_t * file)
{
    return
#if defined(IPGUI_OPERATING_SYSTEM_WINDOWS)
    __ipgui_link_fs(file, &ipgui_fs_win);
#elif defined(IPGUI_OPERATING_SYSTEM_LINUX)
    __ipgui_link_fs(file, &ipgui_fs_linux);
#elif defined(IPGUI_OPERATING_SYSTEM_ELOS)
    __ipgui_link_fs(file, &ipgui_fs_elos);
#else
    IPGUI_ERR_NOK;
#endif
}

/* 建议cache尺寸大于单次读取的大小, 且成倍数关系，不然cache带来的效率提升不明显 */
__IPGUI_API__ ipgui_err_t ipgui_vfs_fopen(ipgui_file_t * file, const char * path, ipgui_file_mode_e mode, unsigned short rcache_size, unsigned int wcache_size)
{
    int ret;
    unsigned short cache_read_size = 0;
    unsigned short cache_write_size = 0;
    unsigned int cache_mng_num = 0;
    void * cache_buffer;
    file->rd_cache = (ipgui_file_cache_t *)0;
    file->wr_cache = (ipgui_file_cache_t *)0;

    if(file->fs->fopen){
        ret = file->fs->fopen(file->fs, path, &file->pri_file, mode);
        if( ret )
        {
            file->status |= IPGUI_FILE_OPEN_ERR;
            return IPGUI_ERR_FS_FOPEN;
        }
    }

    switch(mode)
    {
        case IPGUI_FILE_MODE_READ:
            cache_read_size = IPGUI_ALIGN_CPU(rcache_size);
            if( cache_read_size ) cache_mng_num ++;
            break;
        case IPGUI_FILE_MODE_WRITE:
            cache_write_size = IPGUI_ALIGN_CPU(wcache_size);
            if( cache_write_size ) cache_mng_num ++;
            break;
        case IPGUI_FILE_MODE_APPEND_WRITE:
            cache_write_size = IPGUI_ALIGN_CPU(wcache_size);
            if( cache_write_size ) cache_mng_num ++;
            break;
        case IPGUI_FILE_MODE_APPEND_WRITE_AND_READ:
            cache_read_size = IPGUI_ALIGN_CPU(rcache_size);
            cache_write_size = IPGUI_ALIGN_CPU(wcache_size);
            if( cache_read_size ) cache_mng_num ++;
            if( cache_write_size ) cache_mng_num ++;
            break;
        default:
            return IPGUI_ERR_PARAM;
    }
    /* 
        +------------------------------------+   << low address
        |        read cache manager          |   >> seg1: read cache manager
        +------------------------------===---+
        |        write cache manager         |   >> seg2: write cache manager
        +------------------------------------+
        |           read cache               |   >> seg3: read cache
        +------------------------------------+
        |           write cache              |   >> seg4: write cache
        +------------------------------------+   << high address
    */
    cache_buffer = IPGUI_FS_MEM_ALLOC(cache_read_size + cache_write_size + cache_mng_num * IPGUI_ALIGN(sizeof(ipgui_file_cache_t), IPGUI_MEM_ALIGN_SIZE));
    if( cache_buffer )
    {
        if( cache_read_size ){
            file->rd_cache = (ipgui_file_cache_t *)cache_buffer;
            file->rd_cache->base = (void *)(file->rd_cache + cache_mng_num);
            file->rd_cache->pos = cache_read_size;
            file->rd_cache->end_pos = cache_read_size;
            file->rd_cache->size = cache_read_size; 
            cache_buffer = (void *)(file->rd_cache + 1);
        }

        if( cache_write_size ){
            file->wr_cache = (ipgui_file_cache_t *)cache_buffer;
            file->wr_cache->base = (void *)(((char *)(file->wr_cache + 1)) + cache_read_size);
            file->wr_cache->pos = cache_write_size;
            file->wr_cache->end_pos = cache_write_size;
            file->wr_cache->size = cache_write_size; 
        }
    }

    /* if not enough memory for cache, also return ok */
    return IPGUI_ERR_OK;
}

/* read file */
__IPGUI_API__ ipgui_err_t ipgui_vfs_fread(ipgui_file_t * file, void * buffer, unsigned int btr, unsigned int * br)
{
    int ret;
    char * dptr = buffer;
    unsigned int barrel = 0;

    if( (file->status & 0xff) ==  IPGUI_FILE_OPEN_ERR )  /* test */
        return IPGUI_ERR_FS_FOPEN;

    if( !file->fs->fread )
        return IPGUI_ERR_FS_FREAD;
#if defined(IPGUI_OPERATING_SYSTEM_WINDOWS)
    if( file->rd_cache ){
        int capacity = file->rd_cache->size;
        int last_btr = btr;
        * br = 0;
        if( btr >= capacity ){
            ipgui_dbg_warning("warning: cache size is less than btr\r\n");
            /* if cache size is less than bytes to read, read file with standard api directly */
            goto __std_read;
        }
        while( btr ){
            int cache_left = file->rd_cache->end_pos - file->rd_cache->pos;
            int once_read = cache_left < btr ? cache_left : btr;
            last_btr = btr < once_read ? btr : once_read;
            ipgui_memcpy(dptr, (const)file->rd_cache->base + file->rd_cache->pos, once_read);
            * br += once_read;
            if(!(btr -= once_read))
                break;

            dptr += once_read;
            /* barrel is read size */
            ret = file->fs->fread(file->fs, file->rd_cache->base, capacity, &barrel, file->pri_file);
            if( ret < 0 || barrel == 0)
                break;
            else if((barrel < capacity) && (btr > barrel))
                btr -= btr - barrel;
            file->rd_cache->end_pos = barrel;
            file->rd_cache->pos = 0;
        }
        file->rd_cache->pos = file->rd_cache->pos + last_btr;
    }else
#endif
    {
__std_read:
        ret = file->fs->fread(file->fs, buffer, btr, br, file->pri_file);
        if(ret != 0)
            return IPGUI_ERR_FS_FREAD;
    }
    return IPGUI_ERR_OK;
}

__IPGUI_API__ ipgui_err_t ipgui_vfs_fwrite(ipgui_file_t * file, void * buffer, unsigned int btw, unsigned int * bw)
{
    unsigned int ret;

    if( file->fs->fwrite ){
        ret = file->fs->fwrite(file->fs, buffer, btw, bw, file->pri_file);
        if(ret)
            return IPGUI_ERR_FS_FWRITE;
    }

    return IPGUI_ERR_OK;
}

__IPGUI_API__ ipgui_err_t ipgui_vfs_fclose(ipgui_file_t * file)
{
    int ret;
 
    if(file->rd_cache)
    {
        IPGUI_FS_MEM_FREE(file->rd_cache);
    }else if(file->wr_cache){
        IPGUI_FS_MEM_FREE(file->wr_cache);
    }

    if(file->fs->fclose){
        ret = file->fs->fclose(file->fs, file->pri_file);
        if(ret)
            return IPGUI_ERR_FS_FCLOSE;
    }

    return IPGUI_ERR_OK;
} 

__IPGUI_API__ ipgui_err_t ipgui_vfs_size(ipgui_file_t * file)
{
    int ret;

    if(file->fs->fsize){
        ret = file->fs->fsize(file->fs, file->pri_file);
        if(ret)
            return IPGUI_ERR_FS_MISC;
    }
    
    return IPGUI_ERR_OK;
}

/* left size to read */
__IPGUI_API__ unsigned int ipgui_vfs_get_left(ipgui_file_t * file)
{
    unsigned int ret;

    if(file->fs->frleft){
        return file->fs->frleft(file->fs, file->pri_file);
    }
    
    return 0;
}

__IPGUI_API__ ipgui_err_t ipgui_vfs_delete(ipgui_file_t * file, const char * path)
{
    int ret;

    if(file->fs->fdelete){
        ret = file->fs->fdelete(file->fs, path);
        if(ret)
            return IPGUI_ERR_FS_MISC;
    }
    
    return IPGUI_ERR_OK;
}

__IPGUI_API__ ipgui_err_t ipgui_vfs_rename(ipgui_file_t * file)
{
    int ret;

    if(file->fs->fdelete){
        ret = file->fs->fdelete(file->fs, file->pri_file);
        if(ret)
            return IPGUI_ERR_FS_MISC;
    }
    
    return IPGUI_ERR_OK;
}
__IPGUI_API__ ipgui_err_t ipgui_vfs_puts(ipgui_file_t * file, const char * str, unsigned int * bw)
{
    int ret;

    if(file->fs->fputs){
        ret = file->fs->fputs(file->fs, file->pri_file, bw);
        if(ret)
            return IPGUI_ERR_FS_MISC;
    }
    
    return IPGUI_ERR_OK;
}
__IPGUI_API__ ipgui_err_t ipgui_vfs_fseek(ipgui_file_t * file, ipgui_seek_mode_e mode, unsigned int offset)
{
    int ret;

    if(file->fs->fseek){
        ret = file->fs->fseek(file->fs, file->pri_file, mode, offset);
        if(ret)
            return IPGUI_ERR_FS_MISC;
    }
    if( file->rd_cache )
    {
        /* clear read cache */    
        file->rd_cache->pos = file->rd_cache->size;
        file->rd_cache->end_pos = file->rd_cache->size;
    }
    if( file->wr_cache )
    {
        /* clear write cache */    
        file->wr_cache->pos = file->wr_cache->size;
        file->wr_cache->end_pos = file->wr_cache->size;
    }
    return IPGUI_ERR_OK;
}

__IPGUI_API__ ipgui_err_t ipgui_vfs_enter(ipgui_dir_t * dir, const char * path)
{
    int ret;

    if( dir->fs->enter ){
        ret = dir->fs->enter(dir->fs, path, dir->pri_dir);
        if(ret)
            return IPGUI_ERR_FS_DENTER;
    }

    return IPGUI_ERR_OK;
}

__IPGUI_API__ ipgui_err_t ipgui_vfs_mkdir(ipgui_dir_t * dir, const char * path)
{
    int ret;

    if( dir->fs->mkdir ){
        ret = dir->fs->mkdir(dir->fs, path, dir->pri_dir);
        if(ret)
            return IPGUI_ERR_FS_DCREATE;
    }

    return IPGUI_ERR_OK;
}

__IPGUI_API__ ipgui_err_t ipgui_vfs_drename(ipgui_dir_t * dir, const char * path, const char * new_path)
{
    int ret;

    if( dir->fs->drename ){
        ret = dir->fs->drename(dir->fs, path, new_path, dir->pri_dir);
        if(ret)
            return IPGUI_ERR_FS_DRENAME;
    }

    return IPGUI_ERR_OK;
}


#if defined(IPGUI_OPERATING_SYSTEM_WINDOWS)
#include <stdio.h>
/* windows file operations */

#define WINDOW_FCLOSE_OK 0

__IPGUI_STATIC__  int ipgui_win_fopen(ipgui_fs_ops_t * fs, const char * path, void ** pri_file, ipgui_file_mode_e mode)
{
    FILE * f = (FILE *)0;

    /* a+是追加可写+可读，fseek对写无效，对读有效 */
    /* a是只可追加写，不可读 */
    switch((int)mode)
    {
        case IPGUI_FILE_MODE_READ:
            f = fopen(path, "rb");/* 遇到的坑，mode = "r"会读取不完整，改为"rb"就可以了，why? */
            break;
        case IPGUI_FILE_MODE_WRITE:
            f = fopen(path, "w");
            break;
        case IPGUI_FILE_MODE_APPEND_WRITE:
            f = fopen(path, "a");
            break;
        case IPGUI_FILE_MODE_APPEND_WRITE_AND_READ:
            f = fopen(path, "a+");
            break;
        default:
            return -1;
    }

    if (f == (FILE *)0)
        return -1;

    /* 直接把 FILE* 存进 pri_file。
     * 旧实现先从 IPGUI 堆申请一个 8 字节的 FILE** 壳子：壳子被泄漏，
     * 而 close 时又把 fopen 返回的 FILE* 当成池块 free —— 这会把池的
     * 空闲链表写到一个不属于池的地址，整条 free list 直接被踩坏。 */
    * pri_file = (void *)f;

    return 0;
}

__IPGUI_STATIC__ int ipgui_win_fclose(ipgui_fs_ops_t * fs, void * pri_file)
{
    if (!pri_file)
        return -1;
    if (!(WINDOW_FCLOSE_OK == fclose((FILE *)pri_file)))
        return -1;
    /* pri_file 是 fopen 返回的 FILE*，由 CRT 管理，这里不能再 free */
    return 0;
}

__IPGUI_STATIC__ int ipgui_win_fwrite(ipgui_fs_ops_t * fs, void * buffer, unsigned int btw, unsigned int * bw, void * pri_file)
{
    * bw = fwrite(buffer, 1, btw, (FILE *)pri_file);
    return ( (* bw) == 0 ) ? -2/* EOF */ : 0;
}

__IPGUI_STATIC__ int ipgui_win_fread(ipgui_fs_ops_t * fs, void * buffer, unsigned int btr, unsigned int * br, void * pri_file)
{
    * br = fread(buffer, 1, btr, (FILE *)pri_file);
    return 0;
}

__IPGUI_STATIC__ int ipgui_win_fputs(ipgui_fs_ops_t * fs, void * buffer, unsigned int btw, unsigned int * bw, void * pri_file)
{
    * bw = fputs(buffer, (FILE *)pri_file);
    return ( (* bw) == 0 ) ? -2/* EOF */ : 0;
}

__IPGUI_STATIC__ int ipgui_win_fseek(ipgui_fs_ops_t * fs, void * pri_file, ipgui_seek_mode_e mode, int offset)
{
    int ret;
    switch(mode)
    {
        case IPGUI_FILE_SEEK_SET:
            ret = fseek((FILE *)pri_file, offset, SEEK_SET);
            break;
        case IPGUI_FILE_SEEK_CUR:
            ret = fseek((FILE *)pri_file, offset, SEEK_CUR);
            break;
        case IPGUI_FILE_SEEK_END:
            ret = fseek((FILE *)pri_file, offset, SEEK_END);
            break;
    }
    return ret;
}

ipgui_fs_ops_t ipgui_fs_win = {
    .fopen  = ipgui_win_fopen,
    .fread  = ipgui_win_fread,
    .fclose = ipgui_win_fclose,
    .fwrite = ipgui_win_fwrite,
    .fseek  = ipgui_win_fseek,
};

#elif defined(IPGUI_OPERATING_SYSTEM_ELOS)
#include "elfat.h"
__IPGUI_API__  int ipgui_elos_fopen(ipgui_fs_ops_t * fs, const char * path, void * pri_file)
{
    FILE1 ** f = (FILE1 **)IPGUI_FS_MEM_ALLOC(sizeof(FILE1 *));
    if( f == NULL )
        return -1;

    YC_FAT_OpenFile((* f), path);//可读可写，文件必须存在

    pri_file = (void *)(* f);

    return 0;
}

int ipgui_elos_fclose(ipgui_fs_ops_t * fs, void * pri_file)
{
    return YC_FAT_Close((FILE1 *)pri_file);
}

ipgui_fs_ops_t ipgui_fs_elos = {
    .fopen  = ipgui_elos_fopen,
    .fread  = ipgui_elos_fread,
    .fclose = ipgui_elos_fclose,
    .fwrite = ipgui_elos_fwrite,
    .fsize  = ipgui_elos_fsize,
};
/* elfat api:
    int YC_FAT_Read(FILE1* fileInfo,unsigned char * d_buf,unsigned int len);
    FILE1 * YC_FAT_OpenFile(FILE1 * f_op, unsigned char * filepath);
    int YC_FAT_Close(FILE1 * f_cl);
    int YC_FAT_Write(FILE1* fileInfo,unsigned char * d_buf,unsigned int len);
    int YC_FAT_UsrEnterDir(unsigned char *dir1);
    unsigned int YC_FAT_GetCurWorkDir(void);
    int YC_FAT_CreateFile(unsigned char *filepath);
    int YC_FAT_CreateDir(unsigned char *dir);
    unsigned int YC_FAT_TakeFileSize(FILE1 * fl);
    YC_FAT_Del_File(unsigned char *file_path);
    int YC_FAT_DelDir(const char * dir_path);
    int YC_FAT_RenameFile(unsigned char *file_path,unsigned char *file_name);
    int YC_FAT_RenameDir(unsigned char *dir_path,unsigned char *newdir);
    int YC_FAT_MakeFS(unsigned int DiskSecNum,enum PERCLUSZ perclusz);
    int YC_FAT_FileCrop(FILE1 * fl,unsigned int len);
    int YC_FAT_Mount(unsigned char *drvn,ioopr_t *usrdev,char if_mkfs);
    int YC_FAT_Unmount(unsigned char *drvn);
    int YC_FAT_puts(FILE1 *file,const unsigned char * str,Char_sets_t Encode_mode);
    int YC_FAT_Init(struct FilesystemOperations * fatobj);
*/
#elif defined(IPGUI_OPERATING_SYSTEM_LINUX)
#include <stdio.h>
/* linux file operations */

__IPGUI_STATIC__ int ipgui_linux_fopen(ipgui_fs_ops_t * fs, const char * path, void ** pri_file, ipgui_file_mode_e mode)
{
    const char * m;
    switch ((int)mode) {
        case IPGUI_FILE_MODE_READ:                  m = "rb";  break;
        case IPGUI_FILE_MODE_WRITE:                 m = "wb";  break;
        case IPGUI_FILE_MODE_APPEND_WRITE:          m = "ab";  break;
        case IPGUI_FILE_MODE_APPEND_WRITE_AND_READ: m = "ab+"; break;
        default: return -1;
    }
    FILE * f = fopen(path, m);
    if (f == (FILE *)0) return -1;
    * pri_file = (void *)f;
    return 0;
}

__IPGUI_STATIC__ int ipgui_linux_fclose(ipgui_fs_ops_t * fs, void * pri_file)
{
    return (fclose((FILE *)pri_file) == 0) ? 0 : -1;
}

__IPGUI_STATIC__ int ipgui_linux_fwrite(ipgui_fs_ops_t * fs, void * buffer, unsigned int btw, unsigned int * bw, void * pri_file)
{
    * bw = (unsigned int)fwrite(buffer, 1, btw, (FILE *)pri_file);
    return 0;
}

__IPGUI_STATIC__ int ipgui_linux_fread(ipgui_fs_ops_t * fs, void * buffer, unsigned int btr, unsigned int * br, void * pri_file)
{
    * br = (unsigned int)fread(buffer, 1, btr, (FILE *)pri_file);
    return 0;
}

__IPGUI_STATIC__ int ipgui_linux_fseek(ipgui_fs_ops_t * fs, void * pri_file, ipgui_seek_mode_e mode, int offset)
{
    int whence;
    switch (mode) {
        case IPGUI_FILE_SEEK_SET: whence = SEEK_SET; break;
        case IPGUI_FILE_SEEK_CUR: whence = SEEK_CUR; break;
        case IPGUI_FILE_SEEK_END: whence = SEEK_END; break;
        default: return -1;
    }
    return fseek((FILE *)pri_file, offset, whence);
}

__IPGUI_STATIC__ unsigned int ipgui_linux_fsize(ipgui_fs_ops_t * fs, void * pri_file)
{
    long cur, end;
    cur = ftell((FILE *)pri_file);
    fseek((FILE *)pri_file, 0, SEEK_END);
    end = ftell((FILE *)pri_file);
    fseek((FILE *)pri_file, cur, SEEK_SET);
    return (unsigned int)end;
}

__IPGUI_STATIC__ unsigned int ipgui_linux_frleft(ipgui_fs_ops_t * fs, void * pri_file)
{
    unsigned int size = ipgui_linux_fsize(fs, pri_file);
    unsigned int pos  = (unsigned int)ftell((FILE *)pri_file);
    return (size > pos) ? (size - pos) : 0;
}

ipgui_fs_ops_t ipgui_fs_linux = {
    .fopen  = ipgui_linux_fopen,
    .fread  = ipgui_linux_fread,
    .fclose = ipgui_linux_fclose,
    .fwrite = ipgui_linux_fwrite,
    .fseek  = ipgui_linux_fseek,
    .fsize  = ipgui_linux_fsize,
    .frleft = ipgui_linux_frleft,
};

#endif

/* virtual file system test code here

    int read_cache_size = 8;
    int write_cache_size = 1024;
    ipgui_file_t file1 = {.status = 0};
    char usr_data[1024] = {0};
    unsigned int br;
    ipgui_link_fs_auto(&file1);
    ipgui_vfs_fopen(&file1, "./vfs_test", IPGUI_FILE_MODE_READ, read_cache_size, write_cache_size);
    ipgui_vfs_fread(&file1, (void *)usr_data, 4, &br);
    ipgui_vfs_fread(&file1, (void *)usr_data, 4, &br);
    ipgui_vfs_fread(&file1, (void *)usr_data, 2, &br);
    ipgui_vfs_fclose(&file1);
*/