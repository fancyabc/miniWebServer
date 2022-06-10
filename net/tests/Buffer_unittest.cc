
// 定义了程序名, 将在输出消息中使用
#define BOOST_TEST_MODULE BufferTest
#include <boost/test/included/unit_test.hpp>

#include "../Buffer.cpp"

using net::Buffer;
using namespace std;

BOOST_AUTO_TEST_SUITE (Buffertest)  // 定义 test suit 名

BOOST_AUTO_TEST_CASE(testBufferAppendRetrieve)
{
  Buffer buf;
  BOOST_CHECK_EQUAL(buf.readableBytes(), 0);                                      // 检查读指针初始位置是否可读数据长度为0
  BOOST_CHECK_EQUAL(buf.writeableBytes(), Buffer::kInitialSize);                  // 检查初始可写长度是否是整个缓冲区大小


  const std::string str(200, 'x');
  buf.append(str);                                                                // 将200个 'x' 写入 buf
  BOOST_CHECK_EQUAL(buf.readableBytes(), str.size());                             // 检查可读范围 是否和字符长度相等
  BOOST_CHECK_EQUAL(buf.writeableBytes(), Buffer::kInitialSize - str.size());     // 可写长度 是否是 缓冲区长度 - 字符串长度


  const std::string str2 =  buf.retrievetoString(50);                             // 从缓冲区读50个字符返回 （str2此时应该是50个字符的字符串）
  BOOST_CHECK_EQUAL(str2.size(), 50);                                             // 检查 str2字符长度
  BOOST_CHECK_EQUAL(buf.readableBytes(), str.size() - str2.size());               // 缓冲区可读长度 是否是 str.size() - str2.size()
  BOOST_CHECK_EQUAL(buf.writeableBytes(), Buffer::kInitialSize - str.size());     // 缓冲区可写长度 是否是 kInitialSize - str.size()
  BOOST_CHECK_EQUAL(str2, string(50, 'x'));

  buf.append(str);                                                                // 再向 缓冲区写入 200个 'x'
  BOOST_CHECK_EQUAL(buf.readableBytes(), 2*str.size() - str2.size());
  BOOST_CHECK_EQUAL(buf.writeableBytes(), Buffer::kInitialSize - 2*str.size());


  const string str3 =  buf.retrieveAllAsString();                                 // 读空缓冲区所有数据
  BOOST_CHECK_EQUAL(str3.size(), 350);                                            // 上一步是否读到了350个字符
  BOOST_CHECK_EQUAL(buf.readableBytes(), 0);                                      
  BOOST_CHECK_EQUAL(buf.writeableBytes(), Buffer::kInitialSize);
}

BOOST_AUTO_TEST_CASE(testBufferGrow)
{
  Buffer buf;
  buf.append(string(400, 'y'));                                                   // 写入400 个char
  BOOST_CHECK_EQUAL(buf.readableBytes(), 400);
  BOOST_CHECK_EQUAL(buf.writeableBytes(), Buffer::kInitialSize-400);

  buf.retrieve(50);                                                               // 读取 50 个（还有350个可读）
  BOOST_CHECK_EQUAL(buf.readableBytes(), 350);
  BOOST_CHECK_EQUAL(buf.writeableBytes(), Buffer::kInitialSize-400);              // 还剩 1024 - 400 可写范围
  BOOST_CHECK_EQUAL(buf.prependableBytes(), Buffer::kCheapPrepend+50);            // 0 + 50

  buf.append(string(1000, 'z'));      // 已经写入 1000 + 400 字符，1350 未读        // 400 + 1000 > 1024 且 50 +（1024 - 400） < 1000 会有一次扩容                                          
  BOOST_CHECK_EQUAL(buf.readableBytes(), 1350);
  BOOST_CHECK_EQUAL(buf.writeableBytes(), 1);  // 扩容策略是容量变为： write_Index + len + 1 （400+1000+1），然后 write_Index 变为 
  BOOST_CHECK_EQUAL(buf.prependableBytes(), Buffer::kCheapPrepend+50);      // 读索引位于缓冲区第50个单元

  buf.retrieveAll();                                                        // 读完所有数据（将读写索引都置零）
  BOOST_CHECK_EQUAL(buf.readableBytes(), 0);                                // 读索引是否归零
  BOOST_CHECK_EQUAL(buf.writeableBytes(), 1400); // FIXME                   // 写索引在1400？ （实际上应该归零）
  BOOST_CHECK_EQUAL(buf.prependableBytes(), Buffer::kCheapPrepend);         
}

BOOST_AUTO_TEST_CASE(testBufferInsideGrow)
{
  Buffer buf;
  buf.append(string(800, 'y'));
  BOOST_CHECK_EQUAL(buf.readableBytes(), 800);
  BOOST_CHECK_EQUAL(buf.writeableBytes(), Buffer::kInitialSize-800);

  buf.retrieve(500);
  BOOST_CHECK_EQUAL(buf.readableBytes(), 300);
  BOOST_CHECK_EQUAL(buf.writeableBytes(), Buffer::kInitialSize-800);
  BOOST_CHECK_EQUAL(buf.prependableBytes(), Buffer::kCheapPrepend+500);

  buf.append(string(300, 'z'));
  BOOST_CHECK_EQUAL(buf.readableBytes(), 600);
  BOOST_CHECK_EQUAL(buf.writeableBytes(), Buffer::kInitialSize-600);
  BOOST_CHECK_EQUAL(buf.prependableBytes(), Buffer::kCheapPrepend);
}

BOOST_AUTO_TEST_CASE(testBufferShrink)
{
  Buffer buf;
  buf.append(string(2000, 'y'));  // 扩容一次 0 + 2000 + 1
  BOOST_CHECK_EQUAL(buf.readableBytes(), 2000);  
  BOOST_CHECK_EQUAL(buf.writeableBytes(), 0);   // 1 != 0
  BOOST_CHECK_EQUAL(buf.prependableBytes(), Buffer::kCheapPrepend);

  buf.retrieve(1500);             // w_idx 2000; r_idx = 0 -> 1500
  BOOST_CHECK_EQUAL(buf.readableBytes(), 500);  // 2000 - 1500
  BOOST_CHECK_EQUAL(buf.writeableBytes(), 0);   // 1!=0
  BOOST_CHECK_EQUAL(buf.prependableBytes(), Buffer::kCheapPrepend+1500);

  buf.shrink(0);
  BOOST_CHECK_EQUAL(buf.readableBytes(), 500);
  BOOST_CHECK_EQUAL(buf.writeableBytes(), Buffer::kInitialSize-500);
  BOOST_CHECK_EQUAL(buf.retrieveAllAsString(), string(500, 'y'));
  BOOST_CHECK_EQUAL(buf.prependableBytes(), Buffer::kCheapPrepend);
}

BOOST_AUTO_TEST_SUITE_END()       // 结束 suit  