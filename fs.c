#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define Blocks 4611
#define BlockSize 512
#define InodeSize 64
#define NAME_LEN 15
#define data_begin_block 515
#define Dirsize 32
#define numofaddr (BlockSize / sizeof(_u16))
char PATH[50] = "./disk";

typedef unsigned short _u16;
typedef unsigned long _u64;

typedef struct ext2_group_desc {
    char bg_volume_name[16];
    _u16 bg_block_bitmap;
    _u16 bg_inode_bitmap;
    _u16 bg_inode_table;
    _u16 bg_free_blocks_count;
    _u16 bg_free_inodes_count;
    _u16 bg_used_dirs__count;
    char psw[16];
    char pad[24];
} ext2_group_desc;

//2*(3+8)+8*4+10=64
typedef struct ext2_inode {
    _u16 i_mode;   //文件类型和权限
    _u16 i_blocks; //文件的数据块个数
    _u16 i_size;
    time_t i_atime;
    time_t i_ctime;
    time_t i_mtime;
    time_t i_dtime;
    _u16 i_block[8];
    char pad[10];
} ext2_inode;

//2*4+NAME_LEN+24-NAME_LEN=32
typedef struct ext2_dir_entry {
    _u16 inode;
    _u16 rec_len;
    _u16 name_len;
    _u16 file_type;
    char name[NAME_LEN];
    char pad[24 - NAME_LEN];
} ext2_dir_entry;

/************
*  定义全局变量  *
************/
ext2_group_desc group_desc;
ext2_inode inode;
ext2_dir_entry dir;
_u16 last_allco_inode = 0;
_u16 last_allco_block = 0;
FILE* f;

char getch()
{
    char ch;
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}
int format()
{
    FILE* fp = NULL;
    int i;
    time_t now;
    time(&now);           //now=当前时间
    char zero[BlockSize]; //zero大小为一个块，用于初始化块为０
    while (fp == NULL)
        fp = fopen(PATH, "w+");
    for (i = 0; i < BlockSize; i++)
        zero[i] = 0;
    for (i = 0; i < Blocks; i++) {
        fseek(fp, i * BlockSize, SEEK_SET);
        fwrite(&zero, BlockSize, 1, fp);
    }

    //初始化组块
    strcpy(group_desc.bg_volume_name, "juanming");
    group_desc.bg_block_bitmap = 1;
    group_desc.bg_inode_bitmap = 2;
    group_desc.bg_inode_table = 3;
    group_desc.bg_free_blocks_count = BlockSize * 8;
    group_desc.bg_free_inodes_count = BlockSize * 8;
    group_desc.bg_used_dirs__count = 1;
    strcpy(group_desc.psw, "\0\0\0");
    fseek(fp, 0, SEEK_SET);
    fwrite(&group_desc, sizeof(ext2_group_desc), 1, fp);

    //初始化数据块位图和索引节点位图
    zero[0] = 0x80;
    fseek(fp, BlockSize, SEEK_SET);
    fwrite(&zero, BlockSize, 1, fp);
    fseek(fp, 2 * BlockSize, SEEK_SET);
    fwrite(&zero, BlockSize, 1, fp);

    //初始化索引节点表，添加第一个索引节点
    inode.i_mode = 2;
    inode.i_blocks = 1;
    inode.i_size = 64;
    inode.i_ctime = now;
    inode.i_atime = now;
    inode.i_mtime = now;
    inode.i_dtime = 0;
    inode.i_block[0] = 0;
    fseek(fp, 3 * BlockSize, SEEK_SET);
    fwrite(&inode, sizeof(ext2_inode), 1, fp);

    //初始化数据块，向第一个数据块写入当前目录的目录项
    dir.inode = 0;
    dir.rec_len = 32;
    dir.name_len = 1;
    dir.file_type = 2;
    strcpy(dir.name, ".");
    fseek(fp, data_begin_block * BlockSize, SEEK_SET);
    fwrite(&dir, sizeof(ext2_dir_entry), 1, fp);

    //写入上一目录的目录项
    dir.inode = 0;
    dir.rec_len = 32;
    dir.name_len = 2;
    dir.file_type = 2;
    strcpy(dir.name, "..");
    fseek(fp, data_begin_block * BlockSize + Dirsize, SEEK_SET);
    fwrite(&dir, sizeof(ext2_dir_entry), 1, fp);

    fclose(fp);
    return 0;
}

int dir_position(ext2_inode* current, int i_size)
{
    _u16 dir_blocks = i_size / BlockSize;
    _u16 offset = i_size % BlockSize;
    _u16 a;
    FILE* fp = NULL;
    if (dir_blocks <= 5)
        return (data_begin_block + current->i_block[dir_blocks]) * BlockSize + offset;
    else {
        while (fp == NULL)
            fp = fopen(PATH, "r+");
        dir_blocks = dir_blocks - 6;
        if (dir_blocks < numofaddr) {
            fseek(fp, (data_begin_block + current->i_block[6]) * BlockSize + dir_blocks * sizeof(_u16), SEEK_SET);
            fread(&a, sizeof(_u16), 1, fp);
            return (data_begin_block + a) * BlockSize + offset;
        } else {
            dir_blocks = dir_blocks - numofaddr;
            fseek(fp, (data_begin_block + current->i_block[7]) * BlockSize + dir_blocks / numofaddr * sizeof(_u16), SEEK_SET);
            fread(&a, sizeof(_u16), 1, fp);
            fseek(fp, (data_begin_block + a) * BlockSize + dir_blocks % numofaddr * sizeof(_u16), SEEK_SET);
            fread(&a, sizeof(int), 1, fp);
            return (data_begin_block + a) * BlockSize + offset;
        }
        fclose(fp);
    }
}

//choice==1时找空闲索引；反之找空闲块
_u16 FindFree(int choice)
{
    FILE* fp = NULL;
    char zero[BlockSize];
    int i = 0;
    while (fp == NULL)
        fp = fopen(PATH, "r+");
    fseek(fp, 0, SEEK_SET);
    fread(&group_desc, sizeof(ext2_group_desc), 1, fp);
    if (choice)
        fseek(fp, 2 * BlockSize, SEEK_SET);
    else
        fseek(fp, BlockSize, SEEK_SET);
    fread(zero, BlockSize, 1, fp);

    _u16 first = 0;
    for (i = first; i < BlockSize; i++) {
        if ((_u16)(zero[i]) != 0xff) {
            _u16 c = 0x0080;
            int j;
            for (j = 0; j < 8; j++) {
                if (!(c & zero[i])) {
                    zero[i] = c | zero[i];
                    if (choice)
                        group_desc.bg_free_inodes_count -= 1;
                    else
                        group_desc.bg_free_blocks_count -= 1;

                    if (choice)
                        fseek(fp, 2 * BlockSize, SEEK_SET);
                    else
                        fseek(fp, BlockSize, SEEK_SET);

                    fwrite(zero, BlockSize, 1, fp);
                    fclose(fp);
                    return (i * 8 + j);
                }
                c = c / 2;
            }
        }
    }
    return 0;
}

//choice==1删除inode;反之删除block
void Del(_u16 num, int choice)
{
    FILE* fp = NULL;
    char zero[BlockSize];
    fp = fopen(PATH, "r+");
    fseek(fp, 0, SEEK_SET);
    fread(&group_desc, sizeof(ext2_group_desc), 1, fp);
    int block_num = (choice == 0 ? group_desc.bg_block_bitmap : group_desc.bg_inode_bitmap);
    fseek(fp, block_num * BlockSize, SEEK_SET);
    fread(zero, BlockSize, 1, fp);
    _u16 i = 0x80;
    int j = 0;
    for (j = 0; j < num % 8; j++)
        i = i / 2;
    zero[num / 8] = zero[num / 8] ^ i; //异或操作
    fseek(fp, block_num * BlockSize, SEEK_SET);
    fwrite(zero, BlockSize, 1, fp);
    fclose(f);
}

//为索引current添加1个block
void add_block(ext2_inode* current)
{
    _u16 addr = FindFree(0);
    FILE* fp = NULL;
    while (fp == NULL)
        fp = fopen(PATH, "r+");
    _u16 i = current->i_blocks;
    if (i < 6)
        current->i_block[i] = addr;
    else if (i < 6 + numofaddr) { //一级索引可以容纳
        if (i == 6)
            current->i_block[6] = FindFree(0); //还未创建一级索引则创建
        int sk = (data_begin_block + current->i_block[6]) * BlockSize + (i - 6) * sizeof(_u16);
        fseek(fp, sk, SEEK_SET);
        fwrite(&addr, sizeof(_u16), 1, fp);
    } else {
        if (i == 6 + numofaddr) {
            current->i_block[7] = FindFree(0);
            _u16 j = FindFree(0);
            fseek(fp, (data_begin_block + current->i_block[7]) * BlockSize, SEEK_SET);
            fwrite(&j, sizeof(_u16), 1, fp);
            fseek(fp, (data_begin_block + j) * BlockSize, SEEK_SET);
            fwrite(&addr, sizeof(_u16), 1, fp);
            return;
        }
        int class2num = i - 6 - numofaddr;
        if (class2num % numofaddr == 0) {
            fseek(fp, (data_begin_block + current->i_block[7]) * BlockSize + class2num / numofaddr * sizeof(_u16), SEEK_SET);
            _u16 j = FindFree(0);
            fwrite(&j, sizeof(_u16), 1, fp);
            fseek(fp, (data_begin_block + j) * BlockSize, SEEK_SET);
            fwrite(&addr, sizeof(_u16), 1, fp);
            return;
        }
        _u16 j;
        fseek(fp, (data_begin_block + current->i_block[7]) * BlockSize + class2num / numofaddr * sizeof(_u16), SEEK_SET);
        fread(&j, sizeof(_u16), 1, fp);
        fseek(fp, (data_begin_block + j) * BlockSize, SEEK_SET);
        fwrite(&addr, sizeof(_u16), 1, fp);
    }
    current->i_blocks++;
}

//返回当前目录索引current中的第num个目录体的绝对地址
int dir_entry_postion(const ext2_inode* current, int num1)
{
    FILE* fp = NULL;
    int location = data_begin_block * BlockSize;
    fp = fopen(PATH, "r+");

    int num = num1 - 1;
    int class0num = num * Dirsize / BlockSize + 1;
    int i_size = num * Dirsize;
    if (class0num <= 6)
        location += current->i_block[class0num - 1] * BlockSize + i_size % BlockSize;
    else if (class0num <= 6 + (int)(numofaddr)) { //一级索引
        int class1num = class0num - 6 - 1;
        fseek(fp, (data_begin_block + current->i_block[6]) * BlockSize + class1num * sizeof(_u16), SEEK_SET);
        _u16 block_location;
        fread(&block_location, sizeof(_u16), 1, fp);
        location += block_location * BlockSize;
        location += i_size % BlockSize;
    } else { //二级索引
        _u16 block_location = current->i_block[7];
        int class2num = class0num - 6 - numofaddr - 1;
        fseek(fp, (data_begin_block + block_location) * BlockSize + class2num / numofaddr * sizeof(_u16), SEEK_SET);
        fread(&block_location, sizeof(_u16), 1, fp);
        fseek(fp, (data_begin_block + block_location) * BlockSize + class2num % numofaddr * sizeof(_u16), SEEK_SET);
        fread(&block_location, sizeof(_u16), 1, fp);
        location += block_location * BlockSize;
        location += i_size % BlockSize;
    }
    fclose(fp);
    return location;
}
//返回目录current的写一下个目录体的绝对地址,并将i_size加上一个目录体的大小
int FindEntry(ext2_inode* current)
{
    FILE* fp = NULL;
    int location;
    fp = fopen(PATH, "r+");
    if (current->i_size % BlockSize == 0)
        add_block(current);
    location = dir_entry_postion(current, current->i_size / Dirsize + 1);
    current->i_size += Dirsize;
    fclose(fp);
    return location;
}

int EnterDir(ext2_inode* current, char* name)
{
    FILE* fp = NULL;
    while (fp == NULL)
        fp = fopen(PATH, "r+");
    int i;
    for (i = 0; i < (current->i_size / Dirsize); i++) {
        fseek(fp, dir_entry_postion(current, i + 1), SEEK_SET);
        fread(&dir, sizeof(ext2_dir_entry), 1, fp);
        if (!strcmp(dir.name, name)) {
            if (dir.file_type == 2) {
                fseek(fp, 3 * BlockSize + dir.inode * sizeof(ext2_inode), SEEK_SET);
                fread(current, sizeof(ext2_inode), 1, fp);
                fclose(fp);
                return 1;
            }
        }
    }
    return 0;
}

/*************
*  用户级函数编写  *
*************/
void Read(ext2_inode* current, char* name)
{
    FILE* fp = NULL;
    int i;
    ext2_dir_entry dir;
    while (fp == NULL)
        fp = fopen(PATH, "r+");
    for (i = 0; i < (current->i_size / Dirsize); i++) {
        fseek(fp, dir_entry_postion(current, i + 1), SEEK_SET);
        fread(&dir, sizeof(ext2_dir_entry), 1, fp);
        if (!strcmp(dir.name, name)) {
            if (dir.file_type == 1) {
                time_t now;
                ext2_inode node;
                char c;
                fseek(fp, 3 * BlockSize + dir.inode * sizeof(ext2_inode), SEEK_SET);
                fread(&node, sizeof(ext2_inode), 1, fp);
                for (i = 0; i < node.i_size; i++) {
                    fseek(fp, dir_position(&node, i), SEEK_SET);
                    fread(&c, sizeof(char), 1, fp);
                    if (c == 0x0d)
                        printf("\n");
                    else
                        printf("%c", c);
                }
                printf("\n");
                time(&now);
                node.i_atime = now;
                fseek(fp, 3 * BlockSize + dir.inode * sizeof(ext2_inode), SEEK_SET);
                fwrite(&node, sizeof(ext2_inode), 1, fp);
                fclose(fp);
                return;
            }
        }
    }
    fclose(fp);
}

int Write(ext2_inode* current, char* name)
{
    FILE* fp = NULL;
    ext2_dir_entry dir;
    ext2_inode node;
    time_t now;
    char str;
    int i;
    fp = fopen(PATH, "r+");
    while (1) {
        for (i = 0; i < (current->i_size / Dirsize); i++) {
            fseek(fp, dir_entry_postion(current, i + 1), SEEK_SET);
            fread(&dir, Dirsize, 1, fp);
            if (!strcmp(dir.name, name)) {
                if (dir.file_type == 1) {
                    fseek(fp, 3 * BlockSize + dir.inode * sizeof(ext2_inode), SEEK_SET);
                    fread(&node, sizeof(ext2_inode), 1, fp);
                    break;
                }
            }
        }
        if (i < (current->i_size / Dirsize))
            break;
        printf("please create it first");
        return 0;
    }
    str = getch();
    while (str != 27) {
        printf("%c", str);
        if (!(node.i_size) % BlockSize)
            add_block(&node);
        fseek(fp, dir_position(&node, node.i_size), SEEK_SET);
        fwrite(&str, sizeof(char), 1, fp);
        node.i_size += sizeof(char);
        if (node.i_size % BlockSize == 0)
            add_block(&node);
        if (str == 0x0d)
            printf("%c", 0x0a);
        str = getch();
    }
    time(&now);
    node.i_mtime = now;
    node.i_atime = now;
    fseek(fp, 3 * BlockSize + dir.inode * sizeof(ext2_inode), SEEK_SET);
    fwrite(&node, sizeof(ext2_inode), 1, fp);
    fclose(fp);
    printf("\n");
    return 0;
}
//在当前目录下删除一个文件或空目录
int Delete(int type, ext2_inode* current, char* name)
{
    FILE* fp = fopen(PATH, "r+");
    int i = 0, flag = 0;
    char zero[Dirsize] = { '\0' };
    int dir_location;
    ext2_inode node;

    for (i = 0; i < current->i_size / Dirsize; i++) {
        fseek(fp, dir_entry_postion(current, i + 1), SEEK_SET);
        fread(&dir, sizeof(ext2_dir_entry), 1, fp);
        if (!strcmp(dir.name, name) && (dir.file_type == type)) {
            flag = 1;
            break;
        }
    }

    if (!flag) {
        printf("file or dir not found!");
        fclose(fp);
        return 0;
    }

    fseek(fp, 3 * BlockSize + dir.inode * sizeof(ext2_inode), SEEK_SET);
    fread(&node, sizeof(ext2_inode), 1, fp);

    if (type == 2) {
        if (node.i_size > 2 * Dirsize) {
            printf("this dir is not empty!");
            return 0;
        }
        Del(node.i_block[0], 0); //删除block
    } else {                     //删文件,要把i_block里的所有块都删了
        int j = 0;
        _u16 a, b;
        for (j = 0; j < 6; j++) {
            if (node.i_blocks == 0)
                break;
            Del(node.i_block[i], 0);
            node.i_blocks--;
        }

        if (node.i_blocks > 0) {
            fseek(fp, (data_begin_block + node.i_block[6]) * BlockSize, SEEK_SET);
            for (j = 0; j < (int)numofaddr; j++) {
                if (node.i_blocks == 0)
                    break;
                fread(&a, sizeof(_u16), 1, fp);
                Del(a, 0);
                node.i_blocks--;
            }
            Del(node.i_block[6], 0);
        }

        if (node.i_blocks > 0) {
            int k;
            for (j = 0; j < (int)numofaddr; j++) {
                fseek(fp, (data_begin_block + node.i_block[7]) * BlockSize + j * sizeof(_u16), SEEK_SET);
                fread(&a, sizeof(_u16), 1, fp);
                fseek(fp, (data_begin_block + a) * BlockSize, SEEK_SET);
                for (k = 0; k < (int)numofaddr; k++) {
                    if (node.i_blocks == 0)
                        break;
                    fread(&b, sizeof(_u16), 1, fp);
                    Del(b, 0);
                    node.i_blocks--;
                }
                Del(a, 0);
            }
            Del(node.i_block[7], 0);
        }
        Del(dir.inode, 1);
    }

    Del(dir.inode, 1); //删除inode;
    //接下来用current中最后一个目录体来填补这个目录体在current中的位置。
    dir_location = dir_entry_postion(current, current->i_size / Dirsize);
    fseek(fp, dir_entry_postion(current, current->i_size / Dirsize), SEEK_SET);
    fread(&dir, sizeof(ext2_dir_entry), 1, fp);
    fseek(fp, dir_entry_postion(current, current->i_size / Dirsize), SEEK_SET);
    fwrite(zero, Dirsize, 1, fp);
    if (dir_location % BlockSize == 0) {
        Del(dir_location / BlockSize, 0);
        current->i_blocks--;
        if (current->i_blocks == 6)
            Del(current->i_block[6], 0);               //删除一级索引
        else if (current->i_blocks == 6 + numofaddr) { //删除二级索引
            _u16 a;
            fseek(fp, (data_begin_block + current->i_block[7]), SEEK_SET);
            fread(&a, sizeof(_u16), 1, fp);
            Del(a, 0);
            Del(current->i_block[7], 0);
        } else if ((current->i_blocks - 6 - numofaddr % numofaddr) == 0) { //删除二级索引中的一块
            _u16 a;
            fseek(fp, (data_begin_block + current->i_block[7]) * BlockSize + ((current->i_blocks - 6 - numofaddr) / numofaddr * sizeof(_u16)), SEEK_SET);
            fread(&a, sizeof(_u16), 1, fp);
            Del(a, 0);
        }
    }
    current->i_size -= Dirsize;

    if (i * Dirsize < current->i_size) { //刚刚要删除的是第i+1个目录体，如果不是最后一个,就填补
        fseek(fp, dir_entry_postion(current, i + 1), SEEK_SET);
        fwrite(&dir, sizeof(ext2_dir_entry), 1, fp);
    }
    if (type == 1)
        printf("the %s is deleted!", name);
    else
        printf("the dir %s is deleted!", name);
    fseek(fp, (data_begin_block + current->i_block[0]) * BlockSize, SEEK_SET);
    fread(&dir, sizeof(ext2_dir_entry), 1, fp);
    fseek(fp, 3 * BlockSize + dir.inode * sizeof(ext2_inode), SEEK_SET);
    fwrite(current, sizeof(ext2_inode), 1, fp);
    fclose(fp);
    return 0;
}

//在当前目录下创建一个新文件或新目录，返回1:已存在同名同类型文件或目录,返回-1:已存在同名不同类型文件或目录
int Create(int type, ext2_inode* current, char* name)
{
    ext2_inode newnode;
    ext2_dir_entry aentry, bentry;
    time_t now;
    time(&now);
    FILE* fp = NULL;
    fp = fopen(PATH, "r+");
    int i = 0;
    _u16 node_location = FindFree(1);
    for (i = 0; i < (current->i_size / Dirsize); i++) {
        fseek(fp, dir_entry_postion(current, i + 1), SEEK_SET);
        fread(&aentry, sizeof(ext2_dir_entry), 1, fp);
        if (!strcmp(aentry.name, name)) {
            if (aentry.file_type == type)
                return 1;
            else
                return -1;
        }
    }
    fseek(fp, (data_begin_block + current->i_block[0]) * BlockSize, SEEK_SET);
    fread(&bentry, sizeof(ext2_dir_entry), 1, fp); //读入当前目录的目录体
    if (type == 1) {
        newnode.i_mode = 1;
        newnode.i_blocks = 0;
        newnode.i_size = 0;
        newnode.i_atime = now;
        newnode.i_ctime = now;
        newnode.i_mtime = now;
        newnode.i_dtime = 0;
        for (i = 0; i < 8; i++)
            newnode.i_block[i] = 0;
        for (i = 0; i < 10; i++)
            newnode.pad[i] = 0xff;
    } else {
        newnode.i_mode = 2;
        newnode.i_blocks = 1;
        newnode.i_size = 2 * Dirsize;
        newnode.i_atime = now;
        newnode.i_ctime = now;
        newnode.i_mtime = now;
        newnode.i_dtime = 0;
        _u16 block_location = FindFree(0); //申请数据块

        char zero[BlockSize]; //清空数据块
        for (i = 0; i < BlockSize; i++)
            zero[i] = 0;
        fseek(fp, (data_begin_block + block_location) * BlockSize, SEEK_SET);
        fwrite(&zero, BlockSize, 1, fp);

        newnode.i_block[0] = block_location;
        for (i = 1; i < 8; i++)
            newnode.i_block[i] = 0;
        for (i = 0; i < 10; i++)
            newnode.pad[i] = 0xff;
        aentry.inode = node_location;
        aentry.rec_len = Dirsize;
        aentry.name_len = 1;
        aentry.file_type = 2;
        strcpy(aentry.name, ".");
        for (i = 0; i < 24 - NAME_LEN; i++)
            aentry.pad[i] = '\0';
        fseek(fp, (data_begin_block + block_location) * BlockSize, SEEK_SET);
        fwrite(&aentry, sizeof(ext2_dir_entry), 1, fp);

        aentry.inode = bentry.inode;
        aentry.rec_len = Dirsize;
        aentry.name_len = 2;
        aentry.file_type = 2;
        strcpy(aentry.name, "..");
        for (i = 0; i < 24 - NAME_LEN; i++)
            aentry.pad[i] = '\0';
        fseek(fp, (data_begin_block + block_location) * BlockSize + Dirsize, SEEK_SET);
        fwrite(&aentry, sizeof(ext2_dir_entry), 1, fp);
    }
    fseek(fp, 3 * BlockSize + node_location * sizeof(ext2_inode), SEEK_SET);
    fwrite(&newnode, sizeof(ext2_inode), 1, fp);
    aentry.inode = node_location;
    aentry.rec_len = Dirsize;
    aentry.name_len = strlen(name);
    aentry.file_type = type;
    strcpy(aentry.name, name);
    for (i = 0; i < 24 - NAME_LEN; i++)
        aentry.pad[i] = '\0';
    int dir_entry_location = FindEntry(current);
    fseek(fp, dir_entry_location, SEEK_SET);
    fwrite(&aentry, sizeof(ext2_inode), 1, fp);
    fseek(fp, 3 * BlockSize + bentry.inode * sizeof(ext2_inode), SEEK_SET);
    fwrite(current, sizeof(ext2_inode), 1, fp);
    fclose(fp);
    return 0;
}

void Ls(const ext2_inode* current)
{
    ext2_dir_entry dir;
    int i, j;
    char timestr[150];
    ext2_inode node;
    f = fopen(PATH, "r+");
    printf("Type\t\tFileName\tCreateTime\t\t\tLastAccessTime\t\t\tModifyTime\n");
    for (i = 0; i < current->i_size / 32; i++) {
        fseek(f, dir_entry_postion(current, i + 1), SEEK_SET);
        fread(&dir, sizeof(ext2_dir_entry), 1, f);
        fseek(f, group_desc.bg_inode_table * BlockSize + dir.inode * sizeof(ext2_inode), SEEK_SET);
        fread(&node, sizeof(ext2_inode), 1, f);
        strcpy(timestr, "");
        strcat(timestr, asctime(localtime(&node.i_ctime)));
        strcat(timestr, asctime(localtime(&node.i_atime)));
        strcat(timestr, asctime(localtime(&node.i_mtime)));
        for (j = 0; j < (int)(strlen(timestr) - 1); j++)
            if (timestr[j] == '\n')
                timestr[j] = '\t';

        if (dir.file_type == 1)
            printf("File       \t");
        else
            printf("Directory\t");
        printf("%s\t\t%s", dir.name, timestr);
    }
    fclose(f);
}

void init(ext2_inode* current)
{
    f = fopen(PATH, "r+");
    fseek(f, 0, SEEK_SET);
    fread(&group_desc, sizeof(ext2_group_desc), 1, f);
    fseek(f, 3 * BlockSize, SEEK_SET);
    fread(current, sizeof(ext2_inode), 1, f);
    fclose(f);
}

void getname(char* name, ext2_inode current)
{
    ext2_inode node = current;
    ext2_dir_entry dir;
    f = fopen(PATH, "r+");
    EnterDir(&node, "..");
    int i, j;
    fseek(f, dir_entry_postion(&current, 1), SEEK_SET);
    fread(&dir, sizeof(ext2_dir_entry), 1, f);
    j = dir.inode;
    for (i = 0; i < node.i_size / Dirsize; i++) {
        fseek(f, dir_entry_postion(&node, i + 1), SEEK_SET);
        fread(&dir, sizeof(ext2_dir_entry), 1, f);
        if (dir.inode == j) {
            strcpy(name, dir.name);
            name[dir.name_len] = '\0';
            return;
        }
    }
}
int getpath(char* cpath, int pathlen, ext2_inode current)
{
    char name[10];
    int index = pathlen - 1;
    cpath[index] = '\0';
    getname(name, current);
    index -= strlen(name);
    while (strcmp(name, ".")) {
        memcpy(cpath + index, name, strlen(name));
        index--;
        memcpy(cpath + index, "/", 1);
        EnterDir(&current, "..");
        getname(name, current);
        index -= strlen(name);
    }
    index -= 4;
    memcpy(cpath + index, "/root", 5);
    return index;
}

int alzpath(char* pre, int i, char* path)
{
    char *index1 = path + i, *index2 = path + i;
    while (*(index2) != '/' && i < strlen(path)) {
        index2++;
        i++;
    }
    while (index1 != index2) {
        *(pre) = *(index1);
        pre++;
        index1++;
    }
    *pre = '\0';
    return i + 1;
}

void Remove(ext2_inode* dir, char* path)
{
    ext2_inode* nowroot;
    ext2_inode nowdir;
    memcpy(&nowdir, dir, sizeof(ext2_inode));
    if (*path == '/') {
        nowroot = (ext2_inode*)malloc(sizeof(ext2_inode));
        f = fopen(PATH, "r+");
        fseek(f, 3 * Blocks, SEEK_SET);
        fread(nowroot, sizeof(ext2_inode), 1, f);
    } else
        nowroot = dir;
    char nowpath[10];
    int i = 0, flag = 0, b;
    i = alzpath(nowpath, i, path);
    while (i < strlen(path)) {
        b = EnterDir(nowroot, nowpath);
        if (!b) {
            printf("The path does not exist!");
            memcpy(dir, &nowdir, sizeof(ext2_inode));
            return;
        }
        i = alzpath(nowpath, i, path);
        flag = 1;
    }
    int a = Delete(1, nowroot, nowpath);

    if (flag == 1)
        memcpy(dir, &nowdir, sizeof(ext2_inode));
}

int Cd(ext2_inode* dir, char* path)
{
    int i = 0;
    char nowpath[10];
    ext2_inode pre;
    memcpy(&pre, dir, sizeof(ext2_inode));
    int a = 0;
    i = alzpath(nowpath, i, path);
    while (i < strlen(path)) {
        a = EnterDir(dir, nowpath);
        if (a == 0) {
            printf("path is wrong!");
            memcpy(dir, &pre, sizeof(ext2_inode));
            return 0;
        }
        i = alzpath(nowpath, i, path);
    }
    a = EnterDir(dir, nowpath);
    if (a == 0) {
        printf("path is wrong!");
        memcpy(dir, &pre, sizeof(ext2_inode));
        return 0;
    }
    return 1;
}

void Edit(ext2_inode dir, char* path)
{
    char dirpath[16];
    int i = 0;
    int a = 0;
    for (i = strlen(path); i > 0; i--) {
        if (path[i] == '/')
            break;
    }
    if (i != 0) {
        memcpy(dirpath, path, i);
        dirpath[i] = '\0';
        a = Cd(&dir, dirpath);
        if (a == 0)
            return;
        Write(&dir, path + i + 1);
    } else
        Write(&dir, path);
}
void PassWord()
{
    printf("please input the new psw:");
    char psw[16];
    scanf("%s", psw);
    strcpy(group_desc.psw, psw);
    f = fopen(PATH, "r+");
    fseek(f, 0, SEEK_SET);
    fwrite(&group_desc, sizeof(ext2_group_desc), 1, f);
    fclose(f);
}

void Cat(ext2_inode dir, char* path)
{
    char dirpath[16];
    int i = 0;
    int a = 0;
    for (i = strlen(path); i > 0; i--) {
        if (path[i] == '/')
            break;
    }
    if (i != 0) {
        memcpy(dirpath, path, i);
        dirpath[i] = '\0';
        a = Cd(&dir, dirpath);
        if (a == 0)
            return;
        Read(&dir, path + i + 1);
    } else
        Read(&dir, path);
}

void CreateFilebypath(ext2_inode* dir, char* path)
{
    ext2_inode* nowroot;
    ext2_inode nowdir;
    memcpy(&nowdir, dir, sizeof(ext2_inode));
    if (*path == '/') {
        nowroot = (ext2_inode*)malloc(sizeof(ext2_inode));
        f = fopen(PATH, "r+");
        fseek(f, 3 * Blocks, SEEK_SET);
        fread(nowroot, sizeof(ext2_inode), 1, f);
    } else
        nowroot = dir;
    char nowpath[10];
    int i = 0, flag = 0, b;
    i = alzpath(nowpath, i, path);
    while (i < strlen(path)) {
        b = EnterDir(nowroot, nowpath);
        if (!b) {
            printf("The path does not exist!");
            memcpy(dir, &nowdir, sizeof(ext2_inode));
            return;
        }
        i = alzpath(nowpath, i, path);
        flag = 1;
    }
    int a = Create(1, nowroot, nowpath);
    if (a == -1)
        printf("file is already created!");
    else if (a == 1)
        printf("there is already a dir of the same name!");

    if (flag == 1)
        memcpy(dir, &nowdir, sizeof(ext2_inode));
}
void CreateDirbypath(ext2_inode* dir, char* path)
{
    ext2_inode* nowroot;
    ext2_inode nowdir;
    memcpy(&nowdir, dir, sizeof(ext2_inode));
    if (*path == '/') {
        nowroot = (ext2_inode*)malloc(sizeof(ext2_inode));
        f = fopen(PATH, "r+");
        fseek(f, 3 * Blocks, SEEK_SET);
        fread(nowroot, sizeof(ext2_inode), 1, f);
    } else
        nowroot = dir;
    char nowpath[10];
    int i = 0, flag = 0, b;
    i = alzpath(nowpath, i, path);
    while (i < strlen(path)) {
        b = EnterDir(nowroot, nowpath);
        if (!b) {
            printf("The path does not exist!");
            memcpy(dir, &nowdir, sizeof(ext2_inode));
            return;
        }
        i = alzpath(nowpath, i, path);
        flag = 1;
    }
    int a = Create(2, nowroot, nowpath);
    if (a == -1)
        printf("dir is already created!");
    else if (a == 1)
        printf("there is already a file of the same name!");

    if (flag == 1)
        memcpy(dir, &nowdir, sizeof(ext2_inode));
}

#define printdirname "\033[;34m%s\n\033[0m"
#define printfilename "\033[;33m%s\n\033[0m"
void Tree(ext2_inode current, int depth, int depthnow, int* appear)
{
    if (depth - 1 < depthnow)
        return;
    int i = 0, j = 0;
    ext2_inode next;
    ext2_dir_entry dir;
    f = fopen(PATH, "r+");
    for (i = 2; i < current.i_size / Dirsize; i++) {
        fseek(f, dir_entry_postion(&current, i + 1), SEEK_SET);
        fread(&dir, sizeof(ext2_dir_entry), 1, f);
        if (dir.file_type == 2) {
            for (j = 0; j < depthnow; j++) {
                if (appear[j] == 1)
                    printf("│   ");
                else
                    printf("    ");
            }
            if (i < current.i_size / Dirsize - 1) {
                printf("├── ");
                appear[depthnow] = 1;
            } else {
                printf("└── ");
                appear[depthnow] = 0;
            }
            printf(printdirname, dir.name);
            fseek(f, 3 * BlockSize + dir.inode * sizeof(ext2_inode), SEEK_SET);
            fread(&next, sizeof(ext2_inode), 1, f);
            Tree(next, depth, depthnow + 1, appear);
        } else {
            for (j = 0; j < depthnow; j++) {
                if (appear[j] == 1)
                    printf("│   ");
                else
                    printf("    ");
            }
            if (i < current.i_size / Dirsize - 1)
                printf("├── ");
            else
                printf("└── ");
            printf(printfilename, dir.name);
        }
    }
}

int Login()
{
    printf("Please Input the password:");
    char psw[16];
    scanf("%s", psw);
    if (!strcmp(group_desc.psw, psw)) {
        printf("log in successfully!");
        return 1;
    } else {
        printf("pws is wrong!");
        return 0;
    }
}

void exitdisplay()
{
    printf("thank you for using!\n");
}

void First()
{
    char psw[16];
    printf("you haven'r set your psw,please input:");
    scanf("%s", psw);
    strcpy(group_desc.psw, psw);
    f = fopen(PATH, "r+");
    fseek(f, 0, SEEK_SET);
    fwrite(&group_desc, sizeof(ext2_group_desc), 1, f);
    fclose(f);
}

void Help()
{
    printf("legal commands:");
    printf("create,mkdir,rm,cat,edit,psw,format,exit,login,logout,ls,cd,tree,help");
}

#define printcolorset "\033[;32m\n%s$\033[0m"
void shellloop(ext2_inode currentdir)
{
    if (strlen(group_desc.psw) == 0)
        First();
    char command[20], path[100], current_path[100];
    int appear[10];
    int i, j, var;
    int logflag = 0;
    char ctable[14][14] = { "create", "mkdir", "rm", "cat", "edit", "psw", "format", "exit", "login", "logout", "ls", "cd", "tree", "help" };
    while (1) {
        j = getpath(current_path, 100, currentdir);
        printf(printcolorset, current_path + j);
        scanf("%s", command);
        for (i = 0; i < 14; i++)
            if (!strcmp(command, ctable[i]))
                break;
        if (logflag == 0 && (i < 3 || i == 4 || i == 5 || i == 6 || i == 9)) {
            printf("you must log in to use this command!");
            if (i != 5 && i != 6 && i != 9)
                scanf("%s", command);
            continue;
        }
        switch (i) {
        case 0: //Create File
            scanf("%s", path);
            CreateFilebypath(&currentdir, path);
            break;
        case 1: //Makedir
            scanf("%s", path);
            CreateDirbypath(&currentdir, path);
            break;
        case 2: //Romove
            scanf("%s", path);
            Remove(&currentdir, path);
            break;
        case 3: //Cat
            scanf("%s", path);
            Cat(currentdir, path);
            break;
        case 4: //Edit
            scanf("%s", path);
            Edit(currentdir, path);
            break;
        case 5: //Changepsw
            PassWord();
            break;
        case 6: //Format
            format();
            init(&currentdir);
            break;
        case 7: //Exit
            return;
            break;
        case 8: //Login
            logflag = Login();
            break;
        case 9: //Logout
            logflag = 0;
            printf("log out successfully!");
            break;
        case 10:
            Ls(&currentdir);
            break;
        case 11:
            scanf("%s", path);
            Cd(&currentdir, path);
            break;
        case 12:
            scanf("%d", &var);
            printf(printdirname, ".");
            Tree(currentdir, var, 0, appear);
            break;
        case 13:
            Help();
            break;
        default: {
            printf("illegal	command! use \"help\" to learn more.");
            break;
        }
        }
    }
}

int main()
{

    ext2_inode current;
    init(&current);
    shellloop(current);
    exitdisplay();
    return 0;
}
