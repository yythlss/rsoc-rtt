# 软件包

软件包是通过env下面的packages目录引入的

![image-20260823142520360](D:\TYPORA\figures\image-20260823142520360.png)

![image-20260823145610355](D:\TYPORA\figures\image-20260823145610355.png)

![image-20260823150634994](D:\TYPORA\figures\image-20260823150634994.png)

![image-20260823151127311](D:\TYPORA\figures\image-20260823151127311.png)

# 虚拟文件系统

文件系统是一套实现了数据的存储、分级组织、访问和获取等操作的抽象数据类型 (Abstract data type)，是一种用于向用户提供底层数据访问的机制。文件系统通常存储的基本单位是文件，即数据是按照一个个文件的方式进行组织。当文件比较多时，将导致文件繁多，不易分类、重名的问题。

**DFS设备虚拟文件系统**

### 文件管理

**打开或创建文件**

```c
int open(const char *file, int flags, ...);
```

**关闭文件**

```
int close(int fd);
```

**读取文件内容**

```
int read(int fd, void *buf, size_t len);
```

**向文件写入数据**

```
int write(int fd, const void *buf, size_t len);
```

**重命名文件**

```
int rename(const char *old, const char *new);
```

**获取文件状态**

```
int stat(const char *file, struct stat *buf);
```

**删除文件**

```
int unlink(const char *pathname);
```

**同步文件数据**

```c
int fsync(int fildes);
```

**查询文件系统相关信息**

```c
int statfs(const char *path, struct statfs *buf);
```

### 目录管理

**创建目录**

```c
int mkdir(const char *path, mode_t mode);
```

**删除目录**

```c
int rmdir(const char *pathname);
```

**打开目录**

```c
DIR* opendir(const char* name);
```

**关闭目录**

```c
int closedir(DIR* d);
```

**读取目录**

```c
struct dirent* readdir(DIR *d);
```

![image-20260823154631154](D:\TYPORA\figures\image-20260823154631154.png)