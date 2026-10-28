'use strict';

const path = require('path');
const Mocha = require('mocha');

exports.run = function run() {
  const mocha = new Mocha({ ui: 'bdd', timeout: 30000 });
  mocha.addFile(path.resolve(__dirname, 'suite.js'));
  mocha.run((failures) => {
    process.exitCode = failures ? 1 : 0;
  });
};

