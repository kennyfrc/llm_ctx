#!/usr/bin/env ruby
# Simple Ruby test file for tree-sitter parsing

# A function definition
def hello(name)
  "Hello, #{name}!"
end

# A class with inheritance
class Person < Object
  attr_accessor :name, :age
  
  # Constructor
  def initialize(name, age = 20)
    @name = name
    @age = age
  end
  
  # Instance method
  def greet
    "Hello, my name is #{@name} and I am #{@age} years old."
  end
  
  # Class method
  def self.create(name)
    new(name)
  end
end

# A module
module Greeter
  def self.greet(name)
    "Hello, #{name}!"
  end
  
  # A nested class inside a module
  class Formatter
    def format_greeting(greeting)
      "[Greeting] #{greeting}"
    end
  end
end

# Create some instances
person = Person.new("Alice")
puts person.greet

# Using the module method
puts Greeter.greet("Bob")

# Using the nested class
formatter = Greeter::Formatter.new
puts formatter.format_greeting("Welcome to Ruby!")