#include<iostream>
#include<cstring>

#include"memorypool.h"

//直接调用createPool，更符合c++ oop的思想
MemeryPool::MemeryPool(int max1)
{
    createPool(max1);
}

//初始化MemeryPool 一些适应了使用nginx的人可以调用此API
void MemeryPool::createPool(int max1)
{
    max_=max1;
    //限制小块内存的最大容量
    max_= max_<_maxSize?max_:_maxSize; 
    //区别于nginx中对于max的定义，直接分配max_+sizeof(SmallData)这么多的内存给第一个小块内存池，使得max_的定义更加准确
    small_=(SmallData*)malloc(max_+sizeof(SmallData));
    //对于第一块小内存池的头部赋予属性
    small_->start_=(char *)small_+sizeof(SmallData);
    small_->end_=(char *)small_+max_+sizeof(SmallData)-1;
    small_->next_=nullptr;
    small_->failed_=0;
    //将可调用的第一块小内存池一样赋值
    curSmall_=small_;
    //对于第一块大内存池的头部赋予属性
    large_=nullptr;
    //注意不能这么赋值，因为并没有large_指向的空间，这么做是在操作空指针
    //large_->next_=nullptr;
    //large_->alloc_=nullptr;

    //对用户自定义回收函数的头赋值
    clean_=nullptr;
}

//添加map映射
void MemeryPool::addmp(void *p,int size)
{
    mp_[p]=size;
}

//删除map映射
void MemeryPool::delmp(void *p)
{
    mp_[p]=0;
}

//分配内存(不赋值初值)
void* MemeryPool::pmalloc(size_t size)
{
    void *ret;
    //若用户请求的空间比max_大，则调用pallocLarge
    if(size>max_)   ret= pallocLarge(size);
    //反之，则调用pallocSmall
    else    ret= pallocSmall(size);
    return ret;
}

//分配内存(不赋值初值)（同pmalloc，一些适应了使用nginx的人可以调用此API）
void* MemeryPool::palloc(size_t size)
{
    return pmalloc(size);
}

//分配内存(赋初值0)
void* MemeryPool::pcalloc(size_t nmemb, size_t size)
{
    //调用pmalloc
    char *ret=(char *)pmalloc(size*nmemb);
    //给内存赋初值
    memset(ret,0x00,size*nmemb);
    return (void*)ret;
}

//分配小块内存
void* MemeryPool::pallocSmall(int size)
{
    SmallData *p,*q;
    //返回申请的首地址
    void *ret;
    //从第一个可以调用的小内存池（curSmall_）进行遍历
    for(p=curSmall_;p!=nullptr;p=p->next_)
    {
        //若有满足的小内存池则直接分配
        if(p->end_-p->start_+1>=size)
        {
            ret=p->start_;
            p->start_=p->start_+size;
            //添加映射
            addmp(ret,size);
            return ret;
        }
        //对于不满足的小内存池对其失败次数++，当到达5次的时候，直接不遍历它(修改curSmall_的指向)
        if(p->failed_++>=5) this->curSmall_=p->next_;
        //若没分配成功q指向最后一个内存池节点（方便用于尾插法插入新建的小内存池）
        q=p;
    }
    //若遍历完所有的小内存池则就需要新建空间了
    p=(SmallData*)malloc(max_+sizeof(SmallData));
    if(p==nullptr)  return nullptr;
    //设置新的小内存池的属性
    p->start_=(char *)p+sizeof(SmallData)+size;
    p->end_=(char *)p+sizeof(SmallData)+max_-1;
    p->failed_=0;
    p->next_=nullptr;
    //插入到小内存池队列中
    q->next_=p;
    ret=(char *)p+sizeof(SmallData);
    //添加映射
    addmp(ret,size);
    return ret;
}

//分配大块内存
void* MemeryPool::pallocLarge(int size)
{
    void *ret=malloc(size);
    if(ret==nullptr)    return nullptr;
    int i=0;
    //目的是找已经释放的LargeData头信息，将新malloc的大内存池头部信息放到其中（避免内存浪费，但最多遍历三次）
    for(LargeData *l=this->large_;l!=nullptr && i<=2;l=l->next_,i++)
    {
        if(l->alloc_==nullptr)
        {
            l->alloc_=ret;
            return ret;
        }
    }
    //若没找到已经释放的LargeData头信息则用头插法直接插入到large_的位置，注意这个LargeData是被分配在小内存池中的
    LargeData *q=(LargeData *)pallocSmall(sizeof(LargeData));
    q->alloc_=ret;
    q->next_=this->large_;
    this->large_=q;
    return ret;
}

//释放指定位置内存
bool MemeryPool::pfree(void *p)
{
    //当没有小块内存映射时 证明该块要释放的内存在大块内存中
    if(mp_.count(p)==0) return pfreeLarge(p);
    else    return pfreeSmall(p);
}

//释放大块内存
bool MemeryPool::pfreeLarge(void *p)
{
    for(LargeData *l=this->large_;l!=nullptr;l=l->next_)
    {
        if(l->alloc_!=nullptr && l->alloc_==p)
        {
            free(l->alloc_);
            l->alloc_=nullptr;
            return true;
        }
    }
    return false;
}

//释放小块内存（重置）
bool MemeryPool::pfreeSmall(void *p)
{
    int size=mp_[p];
    if(size==0) return false;
    for(SmallData *s=this->small_;s!=nullptr;s=s->next_)
    {
        //找到小块内存的地址所在块
        if(p>=(char *)s+sizeof(SmallData) && p<=(char *)s+sizeof(SmallData)+max_-1)
        {
            //将这块地址的映射大小重设为0
            mp_[p]=0;
            memset(p,0x00,size);
            //若要释放的地址和未分配的地址是相连的（释放的尾巴连着空白内存头）
            if((char*)p+size==s->start_)
            {
                s->start_=(char*) p;
                //若原本是已经被抛弃的块（不在遍历的块）则重新给机会(头插)
                if(s->failed_>=5)
                {
                    s->next_=this->curSmall_;
                    this->curSmall_=s;
                }
                if(s->next_!=nullptr)   s->failed_=s->next_->failed_;
                else s->failed_=0;
            }
            //若要释放的地址和未分配的地址是相连的（空白内存的尾巴连着释放的头）
            else if((char*)p-1==s->end_)
            {
                s->end_=(char*)p+size-1;
                //若原本是已经被抛弃的块（不在遍历的块）则重新给机会(头插)
                if(s->failed_>=5)
                {
                    s->next_=this->curSmall_;
                    this->curSmall_=s;
                }
                if(s->next_!=nullptr)   s->failed_=s->next_->failed_;
                else s->failed_=0;
            }
            //如果删除之后的地址大小比原本剩余的空间大小大的话，则改变start_和end_的指向
            else if(size> s->end_-s->start_)
            {
                s->start_=(char *)p;
                s->end_=(char *)p+size-1;
                //若原本是已经被抛弃的块（不在遍历的块）则重新给机会
                if(s->failed_>=5)
                {
                    s->next_=this->curSmall_;
                    this->curSmall_=s;
                }
                if(s->next_!=nullptr)   s->failed_=s->next_->failed_;
                else s->failed_=0;
            }
            //（算法效率问题）
            //其他情况则不做考虑（例如先释放了一个比空白内存小的，然后又释放一个和空白内存相连的，若此时他们三个相连了，则不考虑第一次释放的，只是将第二次释放的雨空白的相连）
            return true;
        }
    }
    return false;
}

//重置内存池
void MemeryPool::reset()
{
    //依次释放大内存池
    for(LargeData *l=this->large_;l!=nullptr;l=l->next_)
    {
        if(l->alloc_!=nullptr)
        {
            free(l->alloc_);
            l->alloc_=nullptr;
        }
    }
    //依次释放小内存池（其实并不是释放 而是简单的start_,end_起始地址指针移位）
    for(SmallData *s=this->small_;s!=nullptr;s=s->next_)
    {
        s->start_=(char *)s+sizeof(SmallData);
        s->end_=(char *)s+sizeof(SmallData)+max_-1;
        s->failed_=0;
    }
    //将小块内存映射关系全部删除
    mp_.clear();
    //对于以下元素付初值
    this->curSmall_=small_;
    this->large_=nullptr;
}

//摧毁内存池
void MemeryPool::destory()
{
    //先执行用户预置的内存清理回调函数 (理应还有处理异常的函数)
    for(CleanFunc *c=this->clean_;c!=nullptr;c=c->next_)
    {
        c->func_();
    }
    //依次释放大内存池
    for(LargeData *l=this->large_;l!=nullptr;l=l->next_)
    {
        if(l->alloc_!=nullptr)
        {
            free(l->alloc_);
            l->alloc_=nullptr;
        }
    }
    //依次真正释放小内存池
    for(SmallData *s=this->small_;s!=nullptr;s=s->next_)
    {
        free(s);
    }
    //将小块内存映射关系全部删除
    mp_.clear();
}

