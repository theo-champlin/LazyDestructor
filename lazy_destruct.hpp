#include <cstddef>
#include <queue>
#include <type_traits>
#include <utility>
#include <vector>

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

   static deferred_heap& get()
   {
      thread_local deferred_heap heap;
      return heap;
   }

   void enqueue(element_information info, std::byte* element)
   {
      info.offset = elements.empty() ? 0 : elements.back().offset + elements.back().size;

      if (info.offset + info.size > std::size(heap))
      {
         info.deleter(element);
         return;
      }

      std::copy(element, element + info.size, std::begin(heap) + info.offset);
      elements.emplace(std::move(info));
   }

   bool dequeue()
   {
      if (elements.empty())
      {
         return false;
      }

      const auto& head_element = elements.front();
      head_element.deleter(heap.data() + head_element.offset);
      elements.pop();

      return true;
   }

   void clear()
   {
      while(dequeue());
   }

   ~deferred_heap()
   {
      clear();
   }

private:
   deferred_heap(std::size_t capacity = 512UL) { heap.resize(capacity); }

   std::vector<std::byte> heap;
   std::queue<element_information> elements;
};

template<typename type>
class lazy_destruct
{
public:
   using element_type = type;
   using reference = element_type&;
   using const_reference = const element_type&;
   using pointer = element_type*;
   using const_pointer = const element_type*;

   template<typename...Args>
   lazy_destruct(Args&&... args)
   {
      new (value) element_type{ std::forward < Args &&> (args)...};
   }

   lazy_destruct(lazy_destruct&& other) : value{ std::move(other.value) } {}

   ~lazy_destruct()
   {
      if constexpr(std::is_trivially_destructible_v<element_type>)
      {
         return;
      }

      constexpr auto deleter = [](std::byte * object) noexcept
      {
         reinterpret_cast<element_type*>(object)->~element_type();
      };
      deferred_heap::get().enqueue({ sizeof(element_type), deleter}, value);
   }

   reference operator *() { return *reinterpret_cast<element_type*>(value); }
   const_reference operator *() const { return * reinterpret_cast<const element_type*>(value); }
   pointer operator->() { return reinterpret_cast<element_type*>(value); }
   const_pointer operator->() const { return reinterpret_cast<const element_type*>(value); }

private:
   std::byte value[sizeof(element_type)];
};

// --------------- TESTING CODE ------------------

#include <iostream>
#include <mutex>
#include <thread>

static std::mutex mutex;

template <typename... Args>
void write(Args&&... args)
{
    std::lock_guard lock{ mutex };
    ((std::cout << ... << args) << std::endl);
}

class Noisy
{
    std::size_t value;
public:
    static std::size_t count;

    Noisy() : value{++count} { write("Constructor ", value); }
    Noisy(const Noisy&) { write("Copy constructor ", value); }
    Noisy(Noisy&&) { write("Move constructor ", value); }

    Noisy& operator=(const Noisy&) { write("Copy assignment ", value); return *this; }
    Noisy& operator=(Noisy&&) { write("Move assignment ", value); return *this; }

    ~Noisy() { write("Destructor ", value); }
};

std::size_t Noisy::count = 0;

void helper()
{
    lazy_destruct<Noisy> temp[5];
}

int main()
{
    std::thread split_one(helper);
    helper();

    deferred_heap::get().clear();
    split_one.join();
}
