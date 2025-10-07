#include <iostream>
#include <map>
#include <memory>
#include <type_traits>
#include <utility>
#include <stdexcept>
#include <iterator>
#include <new>         

// пул-аллокатор на куче
template <class T, std::size_t N>
class StaticPoolAllocator {
public:
    using value_type      = T;
    using pointer         = T*;
    using const_pointer   = const T*;
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;

    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_swap            = std::true_type;
    using is_always_equal                        = std::false_type; // у каждого T свой пул

    template <class U> struct rebind { using other = StaticPoolAllocator<U, N>; };

    StaticPoolAllocator() noexcept = default;
    template <class U>
    StaticPoolAllocator(const StaticPoolAllocator<U, N>&) noexcept {}

    pointer allocate(size_type n) {
        // некоторые реализации STL зовут allocate(0)
        if (n == 0) return nullptr;
        if (n != 1) throw std::bad_alloc();

        ensure_pool_();

        // из free-list
        if (state_.free_list) {
            void* p = state_.free_list;
            state_.free_list = state_.free_list->next;
            return static_cast<pointer>(p);
        }

        //  из неиспользованной части пула
        if (state_.used < N) {
            void* p = &state_.pool[state_.used++];
            return static_cast<pointer>(p);
        }

        throw std::bad_alloc();
    }

    void deallocate(pointer p, size_type) noexcept {
        // возвращаем ячейку в free-list
        auto node = reinterpret_cast<FreeNode*>(p);
        node->next = state_.free_list;
        state_.free_list = node;
    }

    template <class U>
    bool operator==(const StaticPoolAllocator<U, N>&) const noexcept { return std::is_same_v<T, U>; }
    template <class U>
    bool operator!=(const StaticPoolAllocator<U, N>& other) const noexcept { return !(*this == other); }

private:
    using storage_t = std::aligned_storage_t<sizeof(T), alignof(T)>;

    struct FreeNode { FreeNode* next; };

    struct State {
        storage_t*  pool      = nullptr; // массив N ячеек на куче
        std::size_t used      = 0;       // сколько выдано 
        FreeNode*   free_list = nullptr; // возвраты поэлементных освобождений

        ~State() {
            if (pool) {
                ::operator delete[](pool, std::align_val_t(alignof(storage_t)));
                pool = nullptr;
            }
        }
    };

    static inline State state_{};

    static void ensure_pool_() {
        if (!state_.pool) {
            state_.pool = static_cast<storage_t*>(
                ::operator new[](sizeof(storage_t) * N,
                                 std::align_val_t(alignof(storage_t)))
            );
            state_.used = 0;
            state_.free_list = nullptr;
        }
    }
};

// простой однонаправленный список параметризуемый аллокатором
template <class T, class Alloc = std::allocator<T>>
class SimpleForwardList {
    struct Node {
        T value;
        Node* next;
        Node(const T& v, Node* n=nullptr) : value(v), next(n) {}
        Node(T&& v, Node* n=nullptr) : value(std::move(v)), next(n) {}
    };

    using NodeAlloc  = typename std::allocator_traits<Alloc>::template rebind_alloc<Node>;
    using NodeTraits = std::allocator_traits<NodeAlloc>;

public:
    using value_type = T;
    using allocator_type = Alloc;

    SimpleForwardList() = default;
    explicit SimpleForwardList(const Alloc& a): alloc_(a) {}
    ~SimpleForwardList() { clear(); }

    SimpleForwardList(const SimpleForwardList&) = delete;
    SimpleForwardList& operator=(const SimpleForwardList&) = delete;

    void push_back(const T& v) { emplace_back(v); }
    void push_back(T&& v)      { emplace_back(std::move(v)); }

    template <class... Args>
    void emplace_back(Args&&... args) {
        Node* n = NodeTraits::allocate(alloc_, 1);
        NodeTraits::construct(alloc_, n, std::forward<Args>(args)..., nullptr);
        if (!head_) { head_ = tail_ = n; }
        else { tail_->next = n; tail_ = n; }
        ++sz_;
    }

    void clear() noexcept {
        Node* cur = head_;
        while (cur) {
            Node* nxt = cur->next;
            NodeTraits::destroy(alloc_, cur);
            NodeTraits::deallocate(alloc_, cur, 1);
            cur = nxt;
        }
        head_ = tail_ = nullptr;
        sz_ = 0;
    }

    bool empty() const noexcept { return sz_ == 0; }
    std::size_t size() const noexcept { return sz_; }

    struct iterator {
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        Node* p = nullptr;
        iterator() = default;
        explicit iterator(Node* n): p(n) {}
        reference operator*() const { return p->value; }
        pointer operator->() const { return &p->value; }
        iterator& operator++() { p = p->next; return *this; }
        iterator operator++(int) { iterator tmp(*this); ++(*this); return tmp; }
        bool operator==(const iterator& r) const { return p == r.p; }
        bool operator!=(const iterator& r) const { return p != r.p; }
    };

    iterator begin() noexcept { return iterator(head_); }
    iterator end() noexcept { return iterator(nullptr); }

private:
    NodeAlloc   alloc_{};
    Node*       head_ = nullptr;
    Node*       tail_ = nullptr;
    std::size_t sz_    = 0;
};

#if defined(_MSC_VER)
constexpr std::size_t kMapOverhead = 2;
#else
constexpr std::size_t kMapOverhead = 1;
#endif

template<std::size_t N>
using MapPoolAlloc = StaticPoolAllocator<std::pair<const int,int>, N + kMapOverhead>;

// демонстрация
static int factorial(int x) {
    int r = 1;
    for (int i = 2; i <= x; ++i) r *= i;
    return r;
}

int main() {
    // std::map со стандартным аллокатором
    std::map<int,int> m1;
    for (int i = 0; i < 10; ++i) m1.emplace(i, factorial(i));

    // std::map с пул-аллокатором на куче, лимит 10 элементов
    std::map<int,int, std::less<>, MapPoolAlloc<10>> m2;
    for (int i = 0; i < 10; ++i) m2.emplace(i, factorial(i));

    // печать
    std::cout << "std::map (std::allocator):\n";
    for (const auto& [k,v] : m1) std::cout << k << ' ' << v << '\n';

    std::cout << "std::map (StaticPoolAllocator, N=10):\n";
    for (const auto& [k,v] : m2) std::cout << k << ' ' << v << '\n';

    // мой контейнер без и с аллокатором
    SimpleForwardList<int> c1;
    for (int i = 0; i < 10; ++i) c1.push_back(i);
    std::cout << "SimpleForwardList<int> (std::allocator):\n";
    for (int x : c1) std::cout << x << '\n';

    using ListAlloc = StaticPoolAllocator<int, 10>;
    SimpleForwardList<int, ListAlloc> c2;
    for (int i = 0; i < 10; ++i) c2.push_back(i);
    std::cout << "SimpleForwardList<int> (StaticPoolAllocator, N=10):\n";
    for (int x : c2) std::cout << x << '\n';

    return 0;
}
