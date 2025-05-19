// Simple TypeScript test file
interface Person {
  name: string;
  age: number;
}

function greet(person: Person): string {
  return `Hello, ${person.name}`;
}

class Employee implements Person {
  name: string;
  age: number;
  department: string;

  constructor(name: string, age: number, department: string) {
    this.name = name;
    this.age = age;
    this.department = department;
  }

  getInfo(): string {
    return `${this.name}, ${this.age}, ${this.department}`;
  }
}
