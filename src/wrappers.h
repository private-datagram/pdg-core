// Copyright (c) 2018 The PrivateDatagram developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WRAPPERS_H
#define WRAPPERS_H

/**
 * Wrapper for using pointer advantages for casting of inheriting objects, so if you are assign new value
 * of inheriting type from first value, a new value will be writen and you can get it value with generic parameters.
 * Example: PtrWrapper<A> a; a = B(); a.get<B>().foo(); B b = a;
 */
template <typename T>
struct PtrWrapper {
private:
    T* value;

    void cleanup() {
        if (value != NULL) {
            delete(value);
            value = NULL;
        }
    }

public:

    PtrWrapper(const PtrWrapper<T> &pt) {
        value = pt.IsNull() ? NULL : new T(pt.get());
    }

    PtrWrapper(const T &t) {
        value = new T(t);
    }

    PtrWrapper() {
        value = new T();
    }

    ~PtrWrapper() {
        cleanup();
    }

    operator T const &() const noexcept { return *value; }

    operator T &() &noexcept { return *value; }

    template<typename I>
    operator I const &() const noexcept { return *((I*)value); }

    template<typename I>
    operator I &() &noexcept { return *((I*)value); }

    T& operator()() { return *value; }

    template<typename I>
    PtrWrapper<T> &operator=(I &val) {
        cleanup();
        value = new I(val);
        return *this;
    }

    template<typename I>
    PtrWrapper<T> &operator=(const I &val) {
        cleanup();
        value = new I(val);
        return *this;
    }

    PtrWrapper<T> &operator=(const PtrWrapper<T> &val) {
        if (val.IsNull()) {
            cleanup();
            return *this;
        }

        *this = val.get();
        return *this;
    }

    T& get() const {
        return *value;
    }

    template<typename I>
    I& get() const {
        return *((I*)(value));
    }

    template<typename I>
    I& getOrRecreate() {
        I* castedValue = dynamic_cast<I*>(value);
        if (castedValue != NULL)
            return *castedValue;

        castedValue = new I();

        cleanup();
        value = castedValue;
        return *castedValue;
    }

    bool IsNull() const {
        return value == NULL;
    }

};

#endif //PDG_CORE_WRAPPERS_H
