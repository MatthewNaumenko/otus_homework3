#include <iostream>
#include <map>
#include <memory>
#include <type_traits>
#include <utility>
#include <stdexcept>
#include <iterator>

// статический пул-аллокатор
// для каждого T и N аллокатор держит статический пул из N ячеек типа T

template <class T, std::size_t N>
class StaticPoolAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_swap            = std::true_type;
    using is_always_equal                        = std::false_type; // у каждого T свой пул

    template <class U> struct rebind { using other = StaticPoolAllocator<U, N>; };

    StaticPoolAllocator() noexcept {}
    template <class U>
    StaticPoolAllocator(const StaticPoolAllocator<U, N>&) noexcept {}

    pointer allocate(size_type n) {
        if (n != 1) {
            throw std::bad_alloc();
        }

        // сначала из списка свободных
        if (free_list_) {
            void* p = free_list_;
            free_list_ = free_list_->next;
            return static_cast<pointer>(p);
        }

        // из неиспользованной части пула
        if (used_ < N) {
            void* p = &pool_[used_++];
            return static_cast<pointer>(p);
        }
        throw std::bad_alloc();
    }

    void deallocate(pointer p, size_type) noexcept {
        auto node = reinterpret_cast<FreeNode*>(p);
        node->next = free_list_;
        free_list_ = node;
    }

    template <class U>
    bool operator==(const StaticPoolAllocator<U, N>&) const noexcept { return std::is_same_v<T, U>; }
    template <class U>
    bool operator!=(const StaticPoolAllocator<U, N>& other) const noexcept { return !(*this == other); }

private:
    // сырые ячейки памяти под T
    using storage_t = std::aligned_storage_t<sizeof(T), alignof(T)>;
    static inline storage_t pool_[N]{};
    static inline std::size_t used_ = 0;

    // узлы списка свободных блоков
    struct FreeNode { FreeNode* next; };
    static inline FreeNode* free_list_ = nullptr;
};

// простой контейнер односвязный список
template <class T, class Alloc = std::allocator<T>>
class SimpleForwardList {
    struct Node {
        T value;
        Node* next;
        Node(const T& v, Node* n=nullptr) : value(v), next(n) {}
        Node(T&& v, Node* n=nullptr) : value(std::move(v)), next(n) {}
    };

    using NodeAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<Node>;
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

    // итератор вперёд
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
    NodeAlloc alloc_{};
    Node* head_ = nullptr;
    Node* tail_ = nullptr;
    std::size_t sz_ = 0;
};

// демонстрация
static int factorial(int x) {
    int r = 1;
    for (int i = 2; i <= x; ++i) r *= i;
    return r;
}

int main() {
    // обычная std::map<int,int> со стандартным аллокатором
    std::map<int,int> m1;
    for (int i = 0; i < 10; ++i) m1.emplace(i, factorial(i));

    // std::map<int,int> с нашим аллокатором, лимит 10 узлов
    using MapAlloc = StaticPoolAllocator<std::pair<const int,int>, 10>;
    std::map<int,int, std::less<>, MapAlloc> m2;
    for (int i = 0; i < 10; ++i) m2.emplace(i, factorial(i));

    // вывод обоих контейнеров
    std::cout << "std::map (std::allocator):\n";
    for (const auto& [k,v] : m1) std::cout << k << ' ' << v << '\n';

    std::cout << "std::map (StaticPoolAllocator, N=10):\n";
    for (const auto& [k,v] : m2) std::cout << k << ' ' << v << '\n';

    // наш контейнер без кастомного аллокатора
    SimpleForwardList<int> c1;
    for (int i = 0; i < 10; ++i) c1.push_back(i);
    std::cout << "SimpleForwardList<int> (std::allocator):\n";
    for (int x : c1) std::cout << x << '\n';

    // наш контейнер c кастомным аллокатором, лимит 10 элементов
    using ListAlloc = StaticPoolAllocator<int, 10>;
    SimpleForwardList<int, ListAlloc> c2;
    for (int i = 0; i < 10; ++i) c2.push_back(i);
    std::cout << "SimpleForwardList<int> (StaticPoolAllocator, N=10):\n";
    for (int x : c2) std::cout << x << '\n';

    return 0;
}
