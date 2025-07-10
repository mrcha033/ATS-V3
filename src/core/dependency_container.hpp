#pragma once

#include <memory>
#include <unordered_map>
#include <typeindex>
#include <functional>
#include <stdexcept>
#include <string>

namespace ats {

class DependencyContainer {
public:
    template<typename T>
    using Factory = std::function<std::shared_ptr<T>()>;
    
    template<typename T>
    using Singleton = std::function<std::shared_ptr<T>()>;

    // Register a transient dependency (new instance each time)
    template<typename Interface, typename Implementation, typename... Args>
    void register_transient() {
        static_assert(std::is_base_of_v<Interface, Implementation>, 
                     "Implementation must inherit from Interface");
        
        auto factory = [this]() -> std::shared_ptr<Interface> {
            return std::make_shared<Implementation>(resolve<Args>()...);
        };
        
        factories_[std::type_index(typeid(Interface))] = 
            [factory]() -> std::shared_ptr<void> {
                return std::static_pointer_cast<void>(factory());
            };
    }

    // Register a singleton dependency (same instance always)
    template<typename Interface, typename Implementation, typename... Args>
    void register_singleton() {
        static_assert(std::is_base_of_v<Interface, Implementation>, 
                     "Implementation must inherit from Interface");
        
        auto factory = [this]() -> std::shared_ptr<Interface> {
            return std::make_shared<Implementation>(resolve<Args>()...);
        };
        
        singletons_[std::type_index(typeid(Interface))] = 
            [factory]() -> std::shared_ptr<void> {
                return std::static_pointer_cast<void>(factory());
            };
    }

    // Register an instance
    template<typename Interface>
    void register_instance(std::shared_ptr<Interface> instance) {
        instances_[std::type_index(typeid(Interface))] = 
            std::static_pointer_cast<void>(instance);
    }

    // Register a factory function
    template<typename Interface>
    void register_factory(Factory<Interface> factory) {
        factories_[std::type_index(typeid(Interface))] = 
            [factory]() -> std::shared_ptr<void> {
                return std::static_pointer_cast<void>(factory());
            };
    }

    // Resolve a dependency
    template<typename T>
    std::shared_ptr<T> resolve() {
        auto type_index = std::type_index(typeid(T));
        
        // Check instances first
        auto instance_it = instances_.find(type_index);
        if (instance_it != instances_.end()) {
            return std::static_pointer_cast<T>(instance_it->second);
        }
        
        // Check singletons
        auto singleton_it = singletons_.find(type_index);
        if (singleton_it != singletons_.end()) {
            auto instance_cache_it = singleton_cache_.find(type_index);
            if (instance_cache_it != singleton_cache_.end()) {
                return std::static_pointer_cast<T>(instance_cache_it->second);
            }
            
            // Create singleton instance
            auto instance = singleton_it->second();
            singleton_cache_[type_index] = instance;
            return std::static_pointer_cast<T>(instance);
        }
        
        // Check transient factories
        auto factory_it = factories_.find(type_index);
        if (factory_it != factories_.end()) {
            return std::static_pointer_cast<T>(factory_it->second());
        }
        
        throw std::runtime_error("No registration found for type: " + 
                                std::string(typeid(T).name()));
    }

    // Try to resolve (returns nullptr if not found)
    template<typename T>
    std::shared_ptr<T> try_resolve() {
        try {
            return resolve<T>();
        } catch (const std::runtime_error&) {
            return nullptr;
        }
    }

    // Check if a type is registered
    template<typename T>
    bool is_registered() const {
        auto type_index = std::type_index(typeid(T));
        return instances_.find(type_index) != instances_.end() ||
               singletons_.find(type_index) != singletons_.end() ||
               factories_.find(type_index) != factories_.end();
    }

    // Clear all registrations
    void clear() {
        instances_.clear();
        singletons_.clear();
        factories_.clear();
        singleton_cache_.clear();
    }

    // Get registration count
    size_t size() const {
        return instances_.size() + singletons_.size() + factories_.size();
    }

private:
    std::unordered_map<std::type_index, std::shared_ptr<void>> instances_;
    std::unordered_map<std::type_index, std::function<std::shared_ptr<void>()>> singletons_;
    std::unordered_map<std::type_index, std::function<std::shared_ptr<void>()>> factories_;
    std::unordered_map<std::type_index, std::shared_ptr<void>> singleton_cache_;
};

// Global container instance
extern DependencyContainer container;

} // namespace ats