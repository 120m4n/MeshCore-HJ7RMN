// Shim mínimo de gtest SOLO para verificar el plan sin PlatformIO.
#pragma once
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
namespace testing { struct Reg { std::string n; std::function<void()> f; }; inline std::vector<Reg>& regs(){static std::vector<Reg> r; return r;} inline int& fails(){static int f=0; return f;}
struct Adder { Adder(const char* a,const char* b,std::function<void()> f){ regs().push_back({std::string(a)+"."+b,f}); } };
inline void InitGoogleTest(int*,char**){}
// GTEST_SKIP() << "motivo";  igual que gtest: sale de la función de test.
struct SkipMsg { template <class T> SkipMsg& operator<<(const T& v){ std::cout << " (skip: " << v << ")"; return *this; } };
struct SkipHelper { void operator=(const SkipMsg&) const { std::printf("\n"); } };
}
#define TEST(a,b) static void a##_##b(); static testing::Adder add_##a##_##b(#a,#b,a##_##b); static void a##_##b()
#define GTEST_SKIP() return testing::SkipHelper() = testing::SkipMsg()
#define EXPECT_(c,msg) do{ if(!(c)){ std::printf("  FAIL %s:%d %s\n",__FILE__,__LINE__,msg); testing::fails()++; } }while(0)
#define EXPECT_EQ(a,b) EXPECT_((a)==(b), #a " == " #b)
#define EXPECT_NE(a,b) EXPECT_((a)!=(b), #a " != " #b)
#define EXPECT_TRUE(a) EXPECT_((a), #a)
#define EXPECT_FALSE(a) EXPECT_(!(a), "!" #a)
#define EXPECT_LE(a,b) EXPECT_((a)<=(b), #a " <= " #b)
#define EXPECT_GE(a,b) EXPECT_((a)>=(b), #a " >= " #b)
#define EXPECT_NEAR(a,b,t) EXPECT_(std::fabs((double)(a)-(double)(b))<=(t), #a " ~ " #b)
#define EXPECT_STREQ(a,b) EXPECT_(std::strcmp((a),(b))==0, #a " streq " #b)
#define ASSERT_TRUE(a) do{ if(!(a)){ std::printf("  FAIL %s:%d %s\n",__FILE__,__LINE__,#a); testing::fails()++; return; } }while(0)
#define ASSERT_(c,msg) do{ if(!(c)){ std::printf("  FAIL %s:%d %s\n",__FILE__,__LINE__,msg); testing::fails()++; return; } }while(0)
#define ASSERT_NE(a,b) ASSERT_((a)!=(b), #a " != " #b)
#define ASSERT_FALSE(a) ASSERT_(!(a), "!" #a)
#define ASSERT_GE(a,b) ASSERT_((a)>=(b), #a " >= " #b)
#define ASSERT_LE(a,b) ASSERT_((a)<=(b), #a " <= " #b)
#define ASSERT_EQ(a,b) do{ if(!((a)==(b))){ std::printf("  FAIL %s:%d %s==%s\n",__FILE__,__LINE__,#a,#b); testing::fails()++; return; } }while(0)
inline int RUN_ALL_TESTS(){ for(auto&r:testing::regs()){ int before=testing::fails(); r.f(); std::printf("%s %s\n", testing::fails()==before?"[ OK ]":"[FAIL]", r.n.c_str()); } std::printf("%d failures\n",testing::fails()); return testing::fails()?1:0; }
