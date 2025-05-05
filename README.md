# Nebula-Toolkit C/C++ 项目规范

一个简单易用的开源C/C++工具库

## 模块概览
+ RingBuffer:线程安全的环形缓冲区
    + 线程安全、线性IO、动态扩容、迭代器支持、0拷贝。
+ ByteStream:基于环形缓冲区的字节序列化工具
    + 继承于RingBuffer，支持自定义类型、vector、string的左右移运算符序列化。
+ LogKit:基于懒汉单例模式的日志库
    + 提供全局宏接口，支持多模式流式打印，提供日志分级，压缩策略、行与函数名开关，最大文件大小配置。

## 提交信息
- feat: 新功能
- docs: 文档更新  
- remove: 删除功能
- update: 功能优化

## 代码风格
使用Google C++代码规范

## 分支管理
- 新功能: feat-<模块名>
- 功能更新: update-<模块名> 
- 主分支: main