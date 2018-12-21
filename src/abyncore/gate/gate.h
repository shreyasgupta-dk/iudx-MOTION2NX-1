#ifndef GATE_H
#define GATE_H

#include <iostream>
#include <vector>

#include "share/share.h"


namespace ABYN::Gates::Interfaces {

    using SharePointer = ABYN::Shares::SharePointer;

//
//  inputs are not defined in the Gate class but in the child classes
//
//  --------
//  |      |
//  | Gate |
//  |      |
//  --------
//     | <- one abstract output
//

    class Gate {
    protected:
        SharePointer output;
        ssize_t gate_id = -1;

    public:
        Gate() { if constexpr (DEBUG) { std::cout << "Gate constructor" << std::endl; }};

        virtual ~Gate() { if constexpr (DEBUG) { std::cout << "Gate destructor" << std::endl; }};

        virtual void Evaluate() = 0;

        virtual SharePointer GetOutputShare() = 0;

        size_t n_parallel_values = 1;
    };

    typedef std::shared_ptr<Gate> SharedGate;


//
//     | <- one abstract input
//  --------
//  |      |
//  | Gate |
//  |      |
//  --------
//     | <- one abstract output
//

    class OneGate : public Gate {
    protected:
        SharePointer parent;

        OneGate() {
            if constexpr (DEBUG) { std::cout << "OneGate constructor" << std::endl; }
        };

        virtual ~OneGate() {
            if constexpr (DEBUG) { std::cout << "OneGate destructor" << std::endl; }
        };

    public:
        virtual void Evaluate() {
            if constexpr (DEBUG) { std::cout << "Evaluate OneGate" << std::endl; }
        }

        virtual SharePointer GetOutputShare() = 0;

    };


//
//     | <- one abstract (perhaps !SharePointer) input
//  --------
//  |      |
//  | Gate |
//  |      |
//  --------
//     | <- SharePointer output
//

    class InputGate : public OneGate {

    protected:
        InputGate() {
            if constexpr (DEBUG) { std::cout << "InputGate constructor" << std::endl; }
        };

        virtual ~InputGate() { if constexpr (DEBUG) { std::cout << "InputGate destructor" << std::endl; }};

    public:

        virtual void Evaluate() {
            if constexpr (DEBUG) { std::cout << "Evaluate InputGate" << std::endl; }
        }

        virtual SharePointer GetOutputShare() = 0;
    };


//
//     | <- one SharePointer input
//  --------
//  |      |
//  | Gate |
//  |      |
//  --------
//     | <- abstract output
//

    class OutputGate : public OneGate {

    protected:
        OutputGate(SharePointer parent) {
            if constexpr (DEBUG) { std::cout << "OutputGate constructor" << std::endl; }
            this->parent = parent;
        };

        virtual ~OutputGate() { if constexpr (DEBUG) { std::cout << "OutputGate destructor" << std::endl; }};

    public:
        virtual void Evaluate() {
            if constexpr (DEBUG) { std::cout << "Evaluate OutputGate" << std::endl; }
        }

        virtual SharePointer GetOutputShare() = 0;
    };

//
//   |    | <- two SharePointers input
//  --------
//  |      |
//  | Gate |
//  |      |
//  --------
//     | <- SharePointer output
//

    class TwoGate : public Gate {

    protected:
        SharePointer parent_a;
        SharePointer parent_b;

    public:
        TwoGate(SharePointer parent_a, SharePointer parent_b) {
            if constexpr (DEBUG) { std::cout << "TwoGate constructor" << std::endl; }
            this->parent_a = parent_a;
            this->parent_b = parent_b;
        };

        virtual ~TwoGate() { if constexpr (DEBUG) { std::cout << "TwoGate destructor" << std::endl; }};

        virtual void Evaluate() {
            if constexpr (DEBUG) { std::cout << "Evaluate TwoGate" << std::endl; }
        }

        virtual SharePointer GetOutputShare() = 0;
    };


//
//  | |... |  <- n SharePointers input
//  --------
//  |      |
//  | Gate |
//  |      |
//  --------
//     | <- SharePointer output
//

    class nInputGate : public Gate {

    protected:
        std::vector<SharePointer> parents;

        nInputGate(std::vector<SharePointer> parents) {
            if constexpr (DEBUG) { std::cout << "nInputGate constructor" << std::endl; }
            this->parents = parents;
        };

    public:
        virtual ~nInputGate() {
            if constexpr (DEBUG) { std::cout << "nInputGate destructor" << std::endl; }
        };

        virtual void Evaluate() {
            if constexpr (DEBUG) { std::cout << "nInputGate TwoGate" << std::endl; }
        }

        virtual SharePointer GetOutputShare() = 0;
    };
}

namespace ABYN::Gates::Arithmetic {
    using namespace ABYN::Shares;

//
//
//     | <- one unsigned integer input
//  --------
//  |      |
//  | Gate |
//  |      |
//  --------
//     | <- one SharePointer(new ArithmeticShare) output
//

//TODO Implement interactive sharing

    template<typename T, typename = std::enable_if_t<std::is_unsigned_v<T>>>
    class ArithmeticInputGate : ABYN::Gates::Interfaces::InputGate {

    protected:
        T value;

    public:
        ArithmeticInputGate(T input) {
            if constexpr (DEBUG) {
                std::cout << "ArithmeticInputGate constructor" << std::endl;
            }
            value = input;
        };

        virtual ~ArithmeticInputGate() {
            if constexpr (DEBUG) {
                std::cout << "ArithmeticInputGate destructor" << std::endl;
            }
        };

        virtual void Evaluate() {
            output = SharePointer(static_cast<Share *>(new ArithmeticShare(value)));
        };

        virtual SharePointer GetOutputShare() { return output; };
    };
}


#endif //GATE_H