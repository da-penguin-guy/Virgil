#ifndef VIRGILLIB_HPP
#define VIRGILLIB_HPP

#include <iostream>
#include <string>
#include <chrono>

namespace VirgilLib {
    class Message {
        public:
            virtual ~Message() = default;
            MessageId id;

            virtual std::string toString() const = 0;
    }

    struct MessageId {
        std::chrono::milliseconds timeSent;
        int messageIndex;

        MessageId(std::string value){
            std::string time = value.substr(0,8);
            messageIndex = std::stoi(value.substring(8,10));
        }

        std::string toString(){
            //TODO: Impliment
        }
    }

}