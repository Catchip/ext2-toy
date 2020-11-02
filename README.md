# 类Ex2文件系统设计

## 磁盘层级(磁盘组织)

### 块

​	为了方便起见,逻辑块大小与物理块大小作为参考均可定义为512字节。根据位图只占用一个块，可得数据块个数以及索引节点个数均为512*8=4096。

### 组描述符

​	为了简单起见，只定义了一个组，组描述符只占用第一个块。

### 编址

​	数据块和索引节点个数为4096,因此地址可以只用两个字节的无符号数就可以描述。

### 索引节点

​	索引节点的大小要满足两个要求：①最好能整除一个块的大小②能够存放下所需要的数据。不妨让索引节点有八个地址域，前六个是直接索引，第七个是一级索引，第八个是二级索引，再考虑到文件类型和创建时间等信息，估算一下64字节足矣。因此，定下索引节点大小为64字节。这样的话，索引节点所占的块数为64*4096/512=512

### 位图

​	数据块位图和索引节点位图均是使用一个块的大小。

### 硬盘

| 组描述符 | 数据块位图 | 索引节点位图 | 索引节点表 | 数据块表 |
| :------: | :--------: | :----------: | :--------: | :------: |
|   1块    |    1块     |     1块      |   512块    |  4096块  |
|   512B   |    512B    |     512B     |   256KB    |   2MB    |

## 程序层级（数据结构)

刚刚提到，只需要两个字节的无符号数就可以定位数据块位置或索引节点位置，也就是编址。可以使用c语言中的unsigned short类型，由于类型名太长，我将其定义为_u16。

```c
typedef unsigned short _u16;
```



### 组描述符

```c
typedef struct ext2_group_desc{
    char bg_volume_name[16];
    _u16 bg_block_bitmap; 
    _u16 bg_inode_bitmap;
    _u16 bg_inode_table;
    _u16 bg_free_blocks_count;
    _u16 bg_free_inodes_count;
    _u16 bg_used_dirs__count;
    char psw[16];
    char pad[24];
}ext2_group_desc;
```

只要组描述符不超过512字节即可。

### 索引节点

```c
typedef struct ext2_inode{
    _u16 i_mode;//文件类型和权限
    _u16 i_blocks;//文件的数据块个数
    _u16 i_size;//文件的大小（以字节为单位)
    time_t i_atime;
    time_t i_ctime;//time_t大小为4个字节
    time_t i_mtime;
    time_t i_dtime;
    _u16 i_block[8];
    char pad[10];
}ext2_inode;
```

​	通过验证time_t大小为4个字节，$4*4+2*(3+8)+10=64$,加上填充域10个字节便为64字节。

### 目录体

```c
typedef struct ext2_dir_entry{
    _u16 inode;//该目录体的索引节点
    _u16 rec_len;
    _u16 name_len;
    _u16 file_type;
    char name[NAME_LEN];
    char pad[24-NAME_LEN];
}ext2_dir_entry;
```

### 关于文件和文件夹

　不管是文件夹还是文件，都对着硬盘中的有且只有一个索引节点`ext2_inode`，通过该索引节点中的i_block上８个域，可以找到该文件夹或文件在硬盘中所分配的所有数据块。对于文件来说，它所分配的数据块中的内容就是一个个字节，不考虑文件本身的格式的话，不含有其他含义；但是对于文件夹来说，它所分配的数据块中的内容是按一个一个的目录体结构组织的。也就是说，它的数据块中的内容就是一个一个的目录体,并且它所分配的第一个数据块的第一个目录体的inode就是自己的inode,第二个目录提的inode是上一层目录的inode。

# 类Ex2文件系统实现

## 初始化硬盘

```c
void format();
```

## 文件系统层函数

```c
int dir_position(ext2_inode* current,int i_size);		//返回current节点所分配的第i_size字节的绝对地址

_u16 FindFree(int choice);		//choice==1时找空闲索引；反之找空闲块，并返回地址

void Del(_u16 num,int choice);		//choice==1删除inode;反之删除block；地址为num

void add_block(ext2_inode *current);		//为current节点增加一个数据块

int dir_entry_postion(const ext2_inode* current,int num1);		//返回current文件夹中的第num1个目录体的绝对地址

int FindEntry(ext2_inode *current);		//返回current文件夹插入新目录体的绝对地址，并将current的大小＋DirSize
```

## 命令层函数

```c
int EnterDir(ext2_inode *current,char *name);//进入current文件夹下的名字为name的文件夹,current更新为指向下一文件夹

void Read(ext2_inode *current,char *name);//对current文件夹下的name文件进行读操作

int Write(ext2_inode* current ,char *name);//对current文件夹下的name文件进行写操作

int Delete(int type,ext2_inode *current,char *name);//在当前目录下删除一个文件或空目录

int Create(int type,ext2_inode * current,char *name);//在当前目录下创建一个新文件或新目录

void getname(char *name,ext2_inode current);//获取当前文件夹名称

void init(ext2_inode *current);//初始化
```

## 用户层函数

```c
void Cat(ext2_inode dir,char *path);

void Ls(const ext2_inode *current);

int Cd(ext2_inode *dir,char *path);

int getpath(char *cpath,int pathlen,ext2_inode current);

void Remove(ext2_inode *dir,char *path);

void CreateFilebypath(ext2_inode *dir,char *path);

void CreateDirbypath(ext2_inode* dir,char *path);

void Edit(ext2_inode dir,char *path)
    
void Tree(ext2_inode current,int depth,int depthnow,int *appear);

int Login();

void PassWord();

void exitdisplay();

void First();

void Help();

void shellloop(ext2_inode currentdir);
```



## 遇到的问题

* 将一些变量更新入磁盘时（将值写入用来模拟磁盘的文件），发现磁盘中的值并没有改变。

  ​	解决方法:将值写入文件的时候并不是在`fwrite`执行时发生的，`fwrite`将流写入缓冲区，真正将缓冲区中的值写入文件是在`fclose`执行时，所以如果要更新文件，最后一定要执行`fclose`。

  

* 利用char型数组和char变量配合的方法对bitmap进行操作时出错。

  ​	解决方法:通过打印相应的值观察现象。由于不能直接对位进行操作，只通过右移、与、或和异或等位运算进行位操作，但是由于char 型变量默认是符号数（最高位为符号位），右移不会改变最高位的值(例如0x80右移一位过后变为0xc0而不是0x40)。只需将char变量改成unsigned short即可。

  

* 出现段错误时.想要利用printf函数进行debug时，发现无法打印，无法定位出错位置。

  ​	解决方法:如果用printf进行debug的话，一定要在打印的内容最后后面加上换行符\n。在我的电脑上打印采用的是行缓冲机制，也就是说，printf中的内容并不是立刻显示到终端的，而是先写入stdout标准输出缓冲区，直到这个缓冲区被装满或者检测到了换行符才会显示到终端。因此，如果出现段错误，程序会立刻终止，但是并不会将缓冲区中还残留的未打印的内容打印到终端。除此之外，还有不缓冲和全缓冲机制。

  

* 利用路径进行文件创建和文件夹创建时，当前的索引节点发生了改变。

  ​	解决方法：由于利用路径进行文件创建的时候，采用的方法是先`cd`到相应的路径，然后再进行文件或文件夹的创建，索引节点发生改变，应该在创建文件之后，恢复索引节点，或者使用索引节点的一个拷贝进行`cd`。在这次实验中，很多地方都要注意到底是要对变量的拷贝进行操作，还是对变量进行操作，这需要了解好传参方式和指针的相关知识

  

* 实现Tree函数是出现不希望的现象

![sendpix0](/home/catchip/Pictures/sendpix0.jpg)

​		解决方法：利用一个appear数组，递归下传。正常结果如下:

![2019-11-20 19-36-01 的屏幕截图](/home/catchip/Pictures/2019-11-20 19-36-01 的屏幕截图.png)