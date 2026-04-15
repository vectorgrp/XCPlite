// SPDX-FileCopyrightText: 2024 Vector Informatik GmbH
//
// SPDX-License-Identifier: MIT

#pragma once

#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <memory>
#include <iomanip>

namespace SilKit {
namespace Util {

//! \brief Parse commandline arguments
class CommandlineParser
{
public:
    CommandlineParser() = default;

    //! \brief Set the application description
    void SetDescription(const std::string& description)
    {
        _appDescription = description;
    }

    //! \brief Declare a new argument
    template <class TArgument, typename... Args>
    auto& Add(Args&&... args)
    {
        auto argument = std::make_unique<TArgument>(std::forward<Args>(args)...);
        if (NameExists(argument->Name()))
        {
            throw std::runtime_error("Cannot add argument '" + argument->Name() + "'. Name already exists.");
        }
        auto shortNameCollision = ShortNameExists(argument->ShortName());
        if (shortNameCollision.first)
        {
            throw std::runtime_error("Cannot add argument '" + argument->Name() + "'. Short name '"
                                     + argument->ShortName() + "' already exists for argument '"
                                     + shortNameCollision.second + "'");
        }

        if (argument->Usage().size() > _longestUsage)
        {
            _longestUsage = argument->Usage().size();
        }
        _arguments.push_back(std::move(argument));
        return *this;
    }

    /*! \brief Retrieve a command line argument by its name
     * \throw SilKit::std::runtime_error when argument does not exist or is of a different kind
     */
    template <class TArgument>
    auto Get(std::string name) -> TArgument&
    {
        auto* argument = GetByName<TArgument>(name);
        if (!argument)
        {
            throw std::runtime_error("Unknown argument '" + name + "'");
        }
        return *argument;
    }

    /*! \brief Output usage info for previously declared parameters to the given stream
     */
    void PrintUsageInfo(std::ostream& out)
    {
        out << std::endl;
        out << _appDescription << std::endl;
        out << std::endl;


        out << "Arguments:" << std::endl;
        for (auto& argument : _arguments)
        {
            if (argument->IsHidden())
                continue;

            if (argument->DescriptionLines().size() > 0)
            {
                out << std::setw(_longestUsage + 1) << std::left << argument->Usage() << "| "
                    << argument->DescriptionLines()[0] << std::endl;
            }
            bool skipFirst = true;
            for (auto& l : argument->DescriptionLines())
            {
                if (skipFirst)
                {
                    skipFirst = false;
                    continue;
                }
                std::cout << std::setw(_longestUsage + 3) << " " << l << std::endl;
            }
        }
        out << std::endl;
    }

    /*! \brief Parse arguments based on argc/argv parameters from a main function
     * \throw SilKit::std::runtime_error when a parsing error occurs
     */
    void ParseArguments(int argc, char** argv)
    {
        auto positionalArgumentIt = std::find_if(_arguments.begin(), _arguments.end(), [](const auto& el) {
            return el->Kind() == ArgumentKind::Positional || el->Kind() == ArgumentKind::PositionalList;
        });
        for (auto i = 1; i < argc; ++i)
        {
            std::string argument{argv[i]};

            auto arg{argument};
            auto isShortForm{false};
            if (arg.length() >= 3 && arg.substr(0, 2) == "--")
            {
                arg.erase(0, 2);
            }
            else if (arg.length() >= 2 && arg.substr(0, 1) == "-")
            {
                arg.erase(0, 1);
                isShortForm = true;
            }
            else if (positionalArgumentIt != _arguments.end())
            {
                if ((*positionalArgumentIt)->Kind() == ArgumentKind::Positional)
                {
                    auto* positionalArgument = static_cast<Positional*>(positionalArgumentIt->get());
                    positionalArgument->_value = std::move(arg);
                    positionalArgumentIt = std::find_if(++positionalArgumentIt, _arguments.end(), [](const auto& el) {
                        return el->Kind() == ArgumentKind::Positional || el->Kind() == ArgumentKind::PositionalList;
                    });
                }
                else
                {
                    auto* positionalArgument = static_cast<PositionalList*>(positionalArgumentIt->get());
                    positionalArgument->_values.push_back(std::move(arg));
                }

                continue;
            }
            else
            {
                throw std::runtime_error("Bad argument '" + argument + "'");
            }

            auto splitPos = std::find(arg.begin(), arg.end(), '=');
            if (splitPos != arg.end())
            {
                std::string name = {arg.begin(), splitPos};
                std::string value = {splitPos + 1, arg.end()};

                auto* option = isShortForm ? GetByShortName<Option>(name) : GetByName<Option>(name);
                if (!option)
                {
                    throw std::runtime_error("Unknown argument '" + argument + "'");
                }
                option->_value = std::move(value);

                continue;
            }

            std::string name = arg;
            auto* option = isShortForm ? GetByShortName<Option>(name) : GetByName<Option>(name);
            if (option)
            {
                if (i + 1 >= argc)
                {
                    throw std::runtime_error("Argument '" + argument + "' without a value");
                }
                std::string value{argv[++i]};
                option->_value = std::move(value);

                continue;
            }

            if (isShortForm)
            {
                // Flag short form (potentially multiple)
                bool foundAny = false;
                for (auto& c : name)
                {
                    auto* flag = GetByShortName<Flag>(std::string{c});
                    if (flag)
                    {
                        foundAny = true;
                        flag->_value = true;
                    }
                    else
                    {
                        throw std::runtime_error("Unknown flag '" + std::string{c} + "'");
                    }
                }
                if (foundAny)
                {
                    continue;
                }
            }
            else
            {
                // Flag long form
                auto* flag = GetByName<Flag>(name);
                if (flag)
                {
                    flag->_value = true;
                    continue;
                }
            }

            throw std::runtime_error("Unknown argument '" + argument + "'");
        }
    }

    enum
    {
        Hidden
    };

    enum class ArgumentKind
    {
        Positional,
        PositionalList,
        Option,
        Flag
    };

    struct IArgument
    {
        virtual ~IArgument() = default;

        virtual auto Kind() const -> ArgumentKind = 0;
        virtual auto Name() const -> std::string = 0;
        virtual auto ShortName() const -> std::string = 0;
        virtual auto Usage() const -> std::string = 0;
        virtual auto DescriptionLines() const -> std::vector<std::string> = 0;
        virtual bool IsHidden() const = 0;
    };

    template <class Derived, class T>
    struct Argument : public IArgument
    {
        Argument(std::string name, std::string shortName, std::string usage, std::vector<std::string> descriptionLines,
                 bool hidden = false)
            : _name(std::move(name))
            , _shortName(std::move(shortName))
            , _usage(std::move(usage))
            , _descriptionLines(std::move(descriptionLines))
            , _hidden{hidden}
        {
        }

        auto Kind() const -> ArgumentKind override
        {
            return static_cast<const Derived*>(this)->kind;
        }
        auto Name() const -> std::string override
        {
            return _name;
        }
        auto ShortName() const -> std::string override
        {
            return _shortName;
        }
        auto Usage() const -> std::string override
        {
            return _usage;
        }
        auto DescriptionLines() const -> std::vector<std::string> override
        {
            return _descriptionLines;
        }
        bool IsHidden() const override
        {
            return _hidden;
        }

    private:
        std::string _name{};
        std::string _shortName{};
        std::string _usage{};
        std::vector<std::string> _descriptionLines;
        bool _hidden{false};
    };

    /*! \brief A positional argument, i.e. one without any prefix, processed in the added order
     *
     * Usage ("<value>") and description ("<value>: Explanation") are used by PrintVersionInfo.
     */
    struct Positional : public Argument<Positional, std::string>
    {
        friend class CommandlineParser;
        static constexpr auto kind = ArgumentKind::Positional;

        Positional(std::string name, std::string usage, std::vector<std::string> descriptionLines)
            : Argument(std::move(name), "", std::move(usage), std::move(descriptionLines))
            , _value()
        {
        }

        auto Value() const -> std::string
        {
            return _value;
        }
        auto HasValue() const -> bool
        {
            return !_value.empty();
        }

    private:
        std::string _value;
    };

    /*! \brief A positional argument to collect multiple string values
     * 
     * It must be the last positional argument that is added, as it collects any remaining positional arguments.
     * Usage ("<value1> [value2 ...]") and description ("<value>, <value2>, ...: Explanation") are used by PrintVersionInfo.
     */
    struct PositionalList : public Argument<PositionalList, std::vector<std::string>>
    {
        friend class CommandlineParser;
        static constexpr auto kind = ArgumentKind::PositionalList;

        PositionalList(std::string name, std::string usage, std::vector<std::string> descriptionLines)
            : Argument(std::move(name), "", std::move(usage), std::move(descriptionLines))
            , _values()
        {
        }

        auto Values() const -> std::vector<std::string>
        {
            return _values;
        }
        auto HasValues() const -> bool
        {
            return !_values.empty();
        }

    private:
        std::vector<std::string> _values;
    };

    /*! \brief A named argument with a string value
     * 
     * It supports a long prefix ("--" followed by name), and a short prefix ("-" followed by shortName), if not empty.
     * The actual value can be suffixed either after a "=" or a " ", i.e. a separate commandline argument.
     * Usage ("[--Name <value>]") and description ("-ShortName, --Name <value>: Explanation") are used by PrintVersionInfo.
     */
    struct Option : public Argument<Option, std::string>
    {
        friend class CommandlineParser;
        static constexpr auto kind = ArgumentKind::Option;

        Option(std::string name, std::string shortName, std::string defaultValue, std::string usage,
               std::vector<std::string> descriptionLines)
            : Argument(std::move(name), std::move(shortName), std::move(usage), std::move(descriptionLines))
            , _defaultValue(std::move(defaultValue))
            , _value()
        {
        }

        Option(std::string name, std::string shortName, std::string defaultValue, std::string usage,
               std::vector<std::string> descriptionLines, decltype(Hidden))
            : Argument(std::move(name), std::move(shortName), std::move(usage), std::move(descriptionLines), true)
            , _defaultValue(std::move(defaultValue))
            , _value()
        {
        }

        auto DefaultValue() const -> std::string
        {
            return _defaultValue;
        }
        auto Value() const -> std::string
        {
            return _value.empty() ? _defaultValue : _value;
        }
        auto HasValue() const -> bool
        {
            return !_value.empty();
        }

    private:
        std::string _defaultValue;
        std::string _value;
    };

    /*! \brief A named argument representing a boolean value
     * 
     * It supports a long prefix ("--" followed by name), and a short prefix ("-" followed by shortName), if not empty.
     * Its value is false until the argument is used.
     * Usage ("[--Name]") and description ("-ShortName, --Name: Explanation") are used by PrintVersionInfo.
     */
    struct Flag : public Argument<Flag, bool>
    {
        friend class CommandlineParser;
        static constexpr auto kind = ArgumentKind::Flag;

        Flag(std::string name, std::string shortName, std::string usage, std::vector<std::string> descriptionLines)
            : Argument(std::move(name), std::move(shortName), std::move(usage), std::move(descriptionLines))
            , _value(false)
        {
        }

        Flag(std::string name, std::string shortName, std::string usage, std::vector<std::string> descriptionLines,
             decltype(Hidden))
            : Argument(std::move(name), std::move(shortName), std::move(usage), std::move(descriptionLines), true)
            , _value(false)
        {
        }

        auto DefaultValue() const -> bool
        {
            return false;
        }
        auto Value() const -> bool
        {
            return _value;
        }

    private:
        bool _value;
    };

private:
    template <class TArgument>
    auto GetByName(std::string name) -> TArgument*
    {
        auto it = std::find_if(_arguments.begin(), _arguments.end(), [&name](const auto& el) {
            return (el->Kind() == TArgument::kind && el->Name() == name);
        });
        return (it != _arguments.end() ? static_cast<TArgument*>(it->get()) : nullptr);
    }

    template <class TArgument>
    auto GetByShortName(std::string name) -> TArgument*
    {
        auto it = std::find_if(_arguments.begin(), _arguments.end(), [&name](const auto& el) {
            return (el->Kind() == TArgument::kind && !static_cast<TArgument&>(*el).ShortName().empty()
                    && static_cast<TArgument&>(*el).ShortName() == name);
        });
        return (it != _arguments.end() ? static_cast<TArgument*>(it->get()) : nullptr);
    }

    auto NameExists(std::string name) -> bool
    {
        auto it =
            std::find_if(_arguments.begin(), _arguments.end(), [&name](const auto& el) { return el->Name() == name; });
        return it != _arguments.end();
    }

    auto ShortNameExists(std::string shortName) -> std::pair<bool, std::string>
    {
        auto it = std::find_if(_arguments.begin(), _arguments.end(),
                               [&shortName](const auto& el) { return el->ShortName() == shortName; });
        bool collision = it != _arguments.end();
        std::string collisionArgName = "";
        if (collision)
        {
            collisionArgName = it->get()->Name();
        }
        return {collision, collisionArgName};
    }

private:
    std::string _appDescription;
    std::vector<std::unique_ptr<IArgument>> _arguments;
    size_t _longestUsage = 0;
};

} // namespace Util
} // namespace SilKit
