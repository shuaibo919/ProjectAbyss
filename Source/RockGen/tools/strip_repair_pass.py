import io

p = r"D:\VibeSpace\ProjectAbyss\Source\RockGen\RockMeshBuilder.cpp"
s = io.open(p, encoding="utf-8").read()

start = s.find("\t\t// Winding fix-up pass \u2014 topological orientation repair")
assert start != -1, "pass start not found"

tail = s[start:]
end = tail.find("\t}\n\n\t}\n} // namespace RockGen")
assert end != -1, "pass end not found"
# Remove the pass block: from its comment up to (and including) the closing '}' before
# the empty line + BuildRock close.
s = s[:start] + "\t}\n} // namespace RockGen\n"
io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("pass removed")
