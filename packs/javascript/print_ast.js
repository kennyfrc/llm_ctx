const Parser = require('tree-sitter');
const JavaScript = require('tree-sitter-javascript');

const parser = new Parser();
parser.setLanguage(JavaScript);

const sourceCode = `
class Person {
  constructor(name) {
    this.name = name;
  }
  
  greet() {
    return 'Hello, ' + this.name;
  }
}
`;

const tree = parser.parse(sourceCode);

function printTree(node, indent = '') {
  const fieldName = node.fieldName ? `${node.fieldName}: ` : '';
  console.log(indent + fieldName + node.type);
  
  for (let i = 0; i < node.childCount; i++) {
    printTree(node.child(i), indent + '  ');
  }
}

printTree(tree.rootNode);