#include<iostream>
#include<memory>
#include<thread>
#include<atomic>
#include<unordered_map>
#include<functional>
class SmallData;
class LargeData;
class MemeryPool;
//最大的小块内存大小（不能超过它，即使用户指定的小块内存比这个大也会向下取到_maxSize）
const int _maxSize=4096;       

class MemeryPool;

//小块内存数据头
class SmallData
{
public:
    friend class MemeryPool;
private:
    //该块小内存池数据真正起始地址
    char *start_;
    //该块小内存池数据结束地址
    char *end_;
    //下一个小内存池地址
    SmallData *next_;
    //记录当前内存池分配失败次数
    short failed_;
};


//大块内存池数据头
class LargeData
{
public:
    friend class MemeryPool;
private:
    //下一个大内存池地址
    LargeData* next_;
    //该块大内存池的起始地址
    void* alloc_;
};

//回收用户自己的空间
class CleanFunc 
{
public:
    friend class MemeryPool;
private:
    template<class F, class ...A>
    void createCleanFunc(F f, A... args)
    {
        //以lambda表达式封装用户自己的函数f和参数到func_中
        func_ = [=]() { f(args...); };
    }
    std::function<void()> func_;
    CleanFunc *next_;
};


class MemeryPool
{
public:
    MemeryPool(){};
    //创建内存池
    MemeryPool(int max1);
    //创建内存池
    void createPool(int max1);
    //用户申请内存
    void* pmalloc(size_t size);
    //就是pmalloc
    void* palloc(size_t size);
    //用户申请全部初始化为0的内存
    void* pcalloc(size_t nmemb, size_t size);
    //释放内存池中指定的数据
    bool pfree(void *p);
    //将内存池中的全部数据释放(重置内存池)
    void reset();
    //摧毁内存池
    void destory();

    //添加处理用户处理回调函数
    template<class F,class ...A>
    void addCleanFunc(F f,A... args)
    {
        //将清理函数信息也分配在小内存池上
        CleanFunc *c=(CleanFunc*)palloc(sizeof(CleanFunc));
        c->createCleanFunc(f,args...);
        c->next_=clean_;
        clean_=c;
    }

private:
    //小块内存最大分配大小（即大小块内存池选择的分界线）
    int max_;
    //指向真正的第一块小块内存池(在内存池启动的时候赋初值并且不在改变)
    SmallData *small_;
    //指向可调用的第一块小内存池
    SmallData *curSmall_;
    //指向第一块大块内存池
    LargeData *large_;
    //指向用户自定义的回收回调函数
    CleanFunc *clean_;
    //小块内存池中内存位置与大小映射（便于用户释放小块内存）
    std::unordered_map<void*,int> mp_;

    //添加小块映射
    void addmp(void *p,int size);
    //删除映射
    void delmp(void *p);
    //申请小块内存池（pmalloc内部调用）
    void* pallocSmall(int size);
    //申请大块内存池（pmalloc内部调用）
    void* pallocLarge(int size);
    //释放内存池中指定的大块数据(pfree内部调用)
    bool pfreeLarge(void *p);
    //释放(重置)内存池中指定的小块数据(pfree内部调用)
    bool pfreeSmall(void *p);
};

