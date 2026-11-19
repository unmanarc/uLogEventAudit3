"use strict";
const fs = require("node:fs");
const path = require("node:path");
const output = path.join(__dirname, "ulogeventaudit3/global/assets");
for (const [source, destination] of [
    ["bootstrap/dist/css/bootstrap.min.css", "css/bootstrap.min.css"],
    ["bootstrap/dist/js/bootstrap.bundle.min.js", "js/bootstrap.bundle.min.js"],
    ["bootstrap/dist/css/bootstrap.min.css.map", "css/bootstrap.min.css.map"],
    ["bootstrap/dist/js/bootstrap.bundle.min.js.map", "js/bootstrap.bundle.min.js.map"],
    ["@popperjs/core/LICENSE.md", "licenses/popper.txt"],
    ["bootstrap/LICENSE", "licenses/bootstrap.txt"],
    ["jquery/dist/jquery.min.js", "js/jquery.min.js"],
    ["jquery/LICENSE.txt", "licenses/jquery.txt"]
]) {
    const target = path.join(output, destination);
    fs.mkdirSync(path.dirname(target), {recursive: true});
    fs.copyFileSync(path.join(__dirname, "node_modules", source), target);
}
console.log("Local Bootstrap and jQuery assets ready.");
