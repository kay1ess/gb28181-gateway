// Resource IDs are opaque; GB identifiers are only used within one platform.
export function buildTree(platform, resources) {
  const nodes = new Map(
    resources
      .filter((resource) => resource.platform_id === platform.id)
      .map((resource) => [resource.id, { id: resource.id, resource, children: [] }]),
  );
  const byGb = new Map([...nodes.values()].map((node) => [node.resource.gb_id, node]));
  const parents = new Map();
  for (const node of nodes.values()) {
    const parent = byGb.get(node.resource.parent_gb_id);
    parents.set(node.id, parent && parent !== node ? parent.id : null);
  }
  // Break malformed parent cycles instead of hiding devices or recursing forever.
  const complete = new Set();
  for (const node of nodes.values()) {
    const path = new Set();
    let current = node.id;
    while (current && !complete.has(current)) {
      if (path.has(current)) {
        parents.set(current, null);
        break;
      }
      path.add(current);
      current = parents.get(current);
    }
    for (const id of path) complete.add(id);
  }
  const roots = [];
  for (const node of nodes.values()) {
    const parent = nodes.get(parents.get(node.id));
    if (parent) parent.children.push(node);
    else roots.push(node);
  }
  return { nodes, parents, roots };
}

export function visibleRows(tree, expanded, { keyword = '', state = '' } = {}) {
  const query = keyword.trim().toLocaleLowerCase();
  const filtering = !!(query || state);
  const visible = new Set();
  if (filtering)
    for (const node of tree.nodes.values()) {
      const resource = node.resource;
      if (state && resource.state !== state) continue;
      if (
        query &&
        ![resource.name, resource.gb_id, resource.id].some((value) =>
          value?.toLocaleLowerCase().includes(query),
        )
      )
        continue;
      let id = node.id;
      while (id && !visible.has(id)) {
        visible.add(id);
        id = tree.parents.get(id);
      }
    }
  const rows = [];
  const stack = tree.roots.map((node) => ({ node, depth: 0 })).reverse();
  while (stack.length) {
    const { node, depth } = stack.pop();
    if (filtering && !visible.has(node.id)) continue;
    const branch = node.children.length > 0 || node.resource.parental;
    const open = branch && (filtering || expanded.has(node.id));
    rows.push({ ...node, depth, branch, open });
    if (open)
      for (let index = node.children.length - 1; index >= 0; index--) {
        stack.push({ node: node.children[index], depth: depth + 1 });
      }
  }
  return rows;
}
