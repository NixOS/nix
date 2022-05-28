#! /usr/bin/env python

# prototype for find-cycles.cc

edges = [
  # sorted:
  #[1, 2],
  #[2, 3],
  #[3, 1],
  # unsorted:
  [1, 2],
  [3, 1],
  [2, 3],
]

edges_yaml = """\
-
 - a-to-c.2.txt
 - c-to-b.2.txt
-
 - b-to-a.2.txt
 - a-to-c.2.txt
-
 - c-to-b.2.txt
 - b-to-a.2.txt
"""

edges = []
i = -1
for line in edges_yaml.splitlines():
  if line == "-":
    i += 1
    continue
  try:
    edges[i]
  except IndexError:
    edges.append([])
  val = line[3:]
  # a: gdmbqa2y7xv2sc0hf6q5c6da3cai5ygw-cyclic-outputs-b/opt/from-b-to-a.2.txt
  # b: b-to-a
  #val = val[112:118]
  edges[i].append(val)

#print(repr(edges)); import sys; sys.exit()

multiedges = []

for edge2 in edges:
  edge2Joined = False
  for edge1 in multiedges:
    print(f"edge2 = {edge2}")
    print(f"edge1 = {edge1}")
    if edge1[-1] == edge2[0]: # edge1.back() == edge2.front()
      # a-b + b-c -> a-b-c
      print(f"append: edge1 = {edge1} + {edge2[1:]} = {edge1 + edge2[1:]}")
      #edge1 = edge1 + edge2[1:] # wrong: this creates a new list
      edge1.append(*edge2[1:])
      print(f"-> edge1 = {edge1}")
      edge2Joined = True
      break
    if edge2[-1] == edge1[0]: # edge2.back() == edge1.front()
      # b-c + a-b -> a-b-c
      print(f"prepend: edge1 = {edge2[:-1]} + {edge1} = {edge2[:-1] + edge1}")
      #edge1.prepend(*edge2[:-1])
      edge1.insert(0, *edge2[:-1])
      print(f"-> edge1 = {edge1}")
      edge2Joined = True
      break
  if not edge2Joined:
    print(f"init: edge1 = {edge2}")
    multiedges.append(edge2)

for edge1 in multiedges:
  print("edge1:")
  for point in edge1:
    print(f"  {point}")

