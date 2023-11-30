---
layout: post
title: C++ move semantics explained
footer-extra: creativecommons.html
tags: [c++, move semantics, unique_ptr, rvalue reference]
---

Move semantics, introduced in C++11, is a powerful feature that allows to move data instead of copying it. Is it that simple? Of course not, C++ has to be complicated!.

![C++ makes people cry](/assets/img/c++_makes_people_cry.jpg)

I like to share this image, but the hidden truth is that C++ makes *me* cry too 😆.

## Move semantics: `std::move` and `Foo&&`, that's all right?

I knew that move doesn't *really* move, and I hated the C++ compiler not raising an error when one does `std::move` and it results in a copy instead 😒.

But until now I almost mechanically used `std::move` and `Foo&&` when I needed to move something. And I knew that I had to move after doing a move:
```c++
struct Foo
{
    Foo() = default;
    Foo(const Foo&) { std::cout << "copy ctor\n"; }
    Foo(Foo&&) { std::cout << "move ctor\n"; }
};

struct Bar
{
    Bar(Foo&& foo) : m_foo(std::move(foo)) {}
private:
    Foo m_foo;
};
```
I thought that was because `Foo&& foo` was an rvalue, but became an lvalue when passed to `m_foo`, so it had to be moved again.

Today, almost twelve years after C++11 was realeased, I found out that move can (should?) go without the rvalue reference.
You'd say: isn't it going to make a copy? Nope 🤨.

## To the code!
I wrote a simple program to test it, leveraging the caracteristics of `std::unique_ptr`.
It consists of a *producer*, a *consumer*, and a *borrower*.

To begin with, we define a `Data` class, which is a simple wrapper around a `std::string`:
```c++
struct Data
{
    Data(const std::string& datum)
        : m_datum(datum)
    {
        std::cout << "[" << this << "] Data::Data()" << std::endl;
    }
    ~Data() { std::cout << "[" << this << "] Data::~Data()" << std::endl; }

    std::string_view datum() const { return m_datum; }

private:
    std::string m_datum;
};
```

Then we define the *consumer* and the *borrower*:
```c++
struct Consumer
{
    void consume(std::unique_ptr<Data> data)
    {
        std::cout << "[CONSUMER] got the data: «" << data->datum() << "»" << std::endl;
    }
};
```

```c++
struct Borrower
{
    void borrow(std::unique_ptr<Data>&& data)
    {
        std::cout << "[BORROWER] got the data: «" << data->datum() << "»" << std::endl;

        // I might steal
        if (m_moocher)
        {
            // and I will!
            auto data_is_mine_now = std::move(data);
        }
    }

    void setBadWill() { m_moocher = true; }

private:
    bool m_moocher { false };
};
```
Note `void consume(std::unique_ptr<Data> data)` as opposite to `void borrow(std::unique_ptr<Data>&& data)`, and mind that `unique_ptr` can't be copied!
Isn't it weird that `consume()` takes a `unique_ptr` by value 🤔?


Let's see the *producer*:
```c++
struct Producer
{
    Producer(Borrower& borrower, Consumer& consumer)
        : m_borrower(borrower)
        , m_consumer(consumer)
    {
    }

    void produce()
    {
        std::cout << "[PRODUCER] producing some data..." << std::endl;
        m_data = std::make_unique<Data>("Hello, World!");

        std::cout << "[PRODUCER] lending the data..." << std::endl;
        m_borrower.borrow(std::move(m_data));

        std::cout << "[PRODUCER] data should be mine again, checking... "
                  << std::boolalpha << (m_data != nullptr) << std::endl;

        if (!m_data)
        {
            std::cout << "[PRODUCER] what a dishonest borrower! "
                      << "If I'll give m_data to the consumer, it will crash!"
                      << std::endl;
        }
        else
        {
            m_consumer.consume(std::move(m_data));
        }

        std::cout << "[PRODUCER] m_data should be nullptr now, checking... "
                  << std::boolalpha << (m_data == nullptr) << std::endl;
    }

private:
    std::unique_ptr<Data> m_data;

    Borrower& m_borrower;
    Consumer& m_consumer;
};
```

Now we begin to understand what's going on.
`Foo&&` isn't called "*rvalue reference*" just because `Foo&` is "*reference*", it is a true reference.
When you have an rvalue reference you may or may not take the ownership of the data, and I'm exercising that with `Borrower::setBadWill()`.

Long story short, we can have two `std::move(m_data)` without any idea whether the data will be moved or not.
Yay 🤡!

---

For the sake of completeness, here's the `main()`:

```c++
int main()
{
    Borrower borrower;
    Consumer consumer;

    std::cout << "\n=== First run ===" << std::endl;
    Producer producer(borrower, consumer);
    producer.produce();

    std::cout << "\n=== Second run ===" << std::endl;
    borrower.setBadWill();
    producer.produce();

    return 0;
}
```
and its output:
```
=== First run ===
[PRODUCER] producing some data...
[0x55dcee0616c0] Data::Data()
[PRODUCER] lending the data...
[BORROWER] got the data: «Hello, World!»
[PRODUCER] data should be mine again, checking... true
[CONSUMER] got the data: «Hello, World!»
[0x55dcee0616c0] Data::~Data()
[PRODUCER] m_data should be nullptr now, checking... true

=== Second run ===
[PRODUCER] producing some data...
[0x55dcee0616c0] Data::Data()
[PRODUCER] lending the data...
[BORROWER] got the data: «Hello, World!»
[0x55dcee0616c0] Data::~Data()
[PRODUCER] data should be mine again, checking... false
[PRODUCER] what a dishonest borrower! If I'll give m_data to the consumer, it will crash!
[PRODUCER] m_data should be nullptr now, checking... true
```

An you'll find the complete code [here](/assets/code/unique-ptr-ownership.cpp) to play with. Have fun!