#include <array>
#include <cstddef>
#include <queue>
#include <type_traits>
#include <utility>

#include <iostream>

//TODO thread safety
class deferred_heap
{
public:
    struct element_information
    {
        element_information(
           std::size_t size,
           void (*deleter)(std::byte*) noexcept)
           :
           size{ size },
           deleter{ deleter }
        {}

        std::size_t size;
        void (*deleter)(std::byte*) noexcept;

        std::size_t offset;
    };

    template <std::size_t capacity = 512>
    static deferred_heap& get()
    {
        static deferred_heap heap{ capacity };
        return heap;
    }

    //TODO ensure noexcept
    void enqueue(element_information info, std::byte* element)
    {
        info.offset = elements.empty() ? 0 : elements.back().offset + elements.back().size;
        std::copy(element, element + info.size, std::begin(heap) + info.offset);
        elements.emplace(std::move(info));
    }

    //TODO ensure noexcept
    bool dequeue()
    {
        if (elements.empty()) return false;

        const auto& head_element = elements.front();
        head_element.deleter(heap.data() + head_element.offset);
        elements.pop();
        return true;
    }

    void clear()
    {
        while(dequeue());
    }

private:
    deferred_heap(std::size_t capacity) { heap.resize(capacity); }

    std::vector<std::byte> heap;

    //TODO make circular
    std::queue<element_information> elements;
};

template <typename type>
class lazy_destruct
{
public:
    using element_type = type;
    using pointer = element_type*;
    using reference = element_type&;

    template <typename... Args>
    lazy_destruct(Args&&... args)
    {
        new (value) element_type{std::forward<Args&&>(args)...};
    }
    lazy_destruct(lazy_destruct&& other) : value{ std::move(other.value) } {}
    ~lazy_destruct()
    {
        const auto deleter = [](std::byte* object) noexcept
        {
            reinterpret_cast<element_type*>(object)->~element_type();
        };
        deferred_heap::get().enqueue({sizeof(element_type), deleter}, value);
    }

    reference operator*() { return *reinterpret_cast<element_type*>(value); }
    const reference operator*() const { return *reinterpret_cast<const element_type*>(value); }
    pointer operator->() { return reinterpret_cast<element_type*>(value); }
    const pointer operator->() const { return reinterpret_cast<const element_type*>(value); }

private:
    std::byte value[sizeof(element_type)];
};

class Noisy
{
    std::size_t value;
public:
    static std::size_t count;

    Noisy() : value{++count} { std::cout << "Constructor" << std::endl; }
    Noisy(const Noisy&) { std::cout << "Copy construct" << std::endl; }
    Noisy(Noisy&&) { std::cout << "Move construct" << std::endl; }

    Noisy& operator=(const Noisy&) { std::cout << "Copy assign" << std::endl; return *this; }
    Noisy& operator=(Noisy&&) { std::cout << "Move assign" << std::endl; return *this; }

    ~Noisy() { std::cout << "Destruct " << value << std::endl; }
};

void helper()
{
    lazy_destruct<Noisy> temp[5];
}

int main()
{
    Noisy::count = 0;
    helper();
    std::cout << "Post helper" << std::endl;
    deferred_heap::get().clear();
}
