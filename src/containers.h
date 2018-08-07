// Copyright (c) 2018 The PrivateDatagram developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WRAPPERS_H
#define WRAPPERS_H

/**
 * Container for storage value as pointer to be able use casting of inheriting objects and it is auto destroy
 * when the scope leaves. So if you are assign new value of inheriting type from first value, a new value will be writen
 * and you can get it value with generic parameters.
 * Example: PtrContainer<A> a; a = B(); a.get<B>().foo(); B b = a;
 */
template <typename T>
struct PtrContainer {
private:
    T* value;

    void cleanup() {
        if (value != NULL) {
            delete(value);
            value = NULL;
        }
    }

public:

    PtrContainer(const PtrContainer<T> &pt) {
        value = pt.get().clone();
    }

    PtrContainer(const T &t) {
        value = t.clone();
    }

    PtrContainer() {
        value = new T();
    }

    ~PtrContainer() {
        cleanup();
    }

    operator T const &() const noexcept { return *value; }

    operator T &() &noexcept { return *value; }

    template<typename T2>
    operator T2 const &() const noexcept { return *((T2*)value); }

    template<typename T2>
    operator T2 &() &noexcept { return *((T2*)value); }

    T& operator()() { return *value; }

    PtrContainer<T> &operator=(const PtrContainer<T> &val) {
        cleanup();
        value = val.get().clone();
        return *this;
    }

    PtrContainer<T> &operator=(PtrContainer<T> &val) {
        return *this = *(const_cast<const PtrContainer<T>*>(&val));
    }

    template<typename T2>
    PtrContainer<T> &operator=(T2 &val) {
        cleanup();
        value = val.clone();
        return *this;
    }

    template<typename T2>
    PtrContainer<T> &operator=(const T2 &val) {
        cleanup();
        value = val.clone();
        return *this;
    }

    T& get() const {
        return *value;
    }

    template<typename T2>
    T2& get() const {
        return *((T2*)(value));
    }

    /**
     * If stored value type is not "T2" then it will be overwritten with new object of "T2" type
     */
    template<typename T2>
    T2& getOrRecreate() {
        T2* castedValue = dynamic_cast<T2*>(value);
        if (castedValue != NULL)
            return *castedValue;

        castedValue = new T2();

        cleanup();
        value = castedValue;
        return *castedValue;
    }

    template<typename T2>
    bool IsInstanceOf() const {
        T2* castedValue = dynamic_cast<T2*>(value);
        return castedValue != NULL;
    }

};

#endif //PDG_CORE_WRAPPERS_H
