#include <iostream>
#include <memory>
#include <string_view>

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

struct Consumer
{
    void consume(std::unique_ptr<Data> data)
    {
        std::cout << "[CONSUMER] got the data: «" << data->datum() << "»" << std::endl;
    }
};

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
