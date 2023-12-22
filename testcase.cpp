#include<iostream>
#include<cstring>
#include<any>
#include "memorypool.h"

using namespace  std;

class Person
{
public:
    Person(){}
    int age_;
    char *name_;
    string *city_;
};

void del(char* name,string *city)
{
    delete [] name;
    delete city;
    cout<<"free ok\n";
}

int main()
{
    MemeryPool pool(1024);
    Person *p=(Person *)pool.pmalloc(sizeof(Person));           
    p->age_=18;
    p->name_=new char[8];
    char name[]="yyt";
    strcpy(p->name_,name);
    p->city_=new string("harbin");
    pool.addCleanFunc(del,p->name_,p->city_);                  
    cout<<p->age_<<" "<<p->name_<<" "<<*p->city_<<endl;         
    pool.destory();
    return 0;
}