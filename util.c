/* See LICENSE file for copyright and license details. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

/**
 * 这个函数的主要作用是打印错误信息并终止程序。它可以处理格式化的错误消息，并在需要时附加系统错误信息。
 * 
 * 函数声明:  这是一个变参函数，意味着它可以接受可变数量的参数。fmt 是一个格式字符串，类似于 printf 的格式字符串。
 * 变量声明: va_list 是一个类型，用于访问变长参数列表。
 * 初始化变长参数列表: va_start 宏初始化 ap 以便访问变长参数。第一个参数是 va_list 变量，第二个参数是最后一个固定参数，即 fmt。
 * 格式化输出错误信息:  vfprintf 函数将格式化的输出写到标准错误流 stderr，使用 fmt 和变长参数 ap。
 * 结束变长参数处理:  va_end 宏用于清理 va_list 变量。
 * 检查格式字符串的最后一个字符:  这里检查 fmt 是否非空且最后一个字符是否为冒号 :。如果是，输出一个空格并调用 perror 打印最近的错误信息。否则，输出一个换行符。
 * 退出程序:  最后，调用 exit(1) 终止程序，返回状态码 1 表示发生错误。
 */
void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt)-1] == ':') {
		fputc(' ', stderr);
		perror(NULL);
	} else {
		fputc('\n', stderr);
	}

	exit(1);
}

void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;

	if (!(p = calloc(nmemb, size)))
		die("calloc:");
	return p;
}
