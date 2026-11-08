"use strict";
const $ = (id) => document.getElementById(id);
let section = "items", page = 0, total = 0, selected = null, locations = [], recipeRows = [], resolveResponse = null, lastRequest = 0;
let queue = Promise.resolve();
let searchTimer;

function send(name, value) {
    if (typeof callEvent === "function") callEvent(name, JSON.stringify(value ?? null));
}
function status(message, error = false) {
    $("status").textContent = message;
    document.querySelector("footer").classList.toggle("error", error);
}
function api(payload) {
    const result = queue.then(async () => {
        await new Promise((resolve) => setTimeout(resolve, Math.max(0, 140 - (Date.now() - lastRequest))));
        lastRequest = Date.now();
        status("Working…");
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => { resolveResponse = null; reject(new Error("No response. Check the result before repeating a grant.")); }, 12000);
            resolveResponse = (value) => {
                clearTimeout(timeout);
                if (value.ok) resolve(value); else reject(new Error(value.message || "Request failed."));
            };
            send("playground:request", payload);
        });
    });
    queue = result.catch(() => {});
    return result;
}
window.addEventListener("playground:response", (event) => {
    const response = event.detail;
    if (!response || typeof response !== "object") return;
    status(response.message || (response.ok ? "Ready." : "Request failed."), !response.ok);
    const resolve = resolveResponse;
    resolveResponse = null;
    resolve?.(response);
});
function action(handler) {
    return (event) => {
        const button = event?.currentTarget instanceof HTMLButtonElement ? event.currentTarget : null;
        if (button) button.disabled = true;
        return Promise.resolve().then(handler).catch((error) => status(error.message, true)).finally(() => {
            if (button) button.disabled = button.id === "prev" ? page === 0 : button.id === "next" ? (page + 1) * 48 >= total : false;
        });
    };
}
function option(value, name) { const row = document.createElement("option"); row.value = value; row.textContent = name; return row; }
function choose(item) {
    selected = item;
    $("selected-name").textContent = item.name;
    $("selected-category").textContent = item.category;
    $("quality").replaceChildren(...Array.from({ length: item.maximumQuality }, (_, index) => option(String(index + 1), `Quality ${index + 1}`)));
    $("spawn-item").disabled = false;
    document.querySelectorAll(".item-row").forEach((row) => { const active = row.dataset.id === item.id; row.classList.toggle("selected", active); row.setAttribute("aria-selected", String(active)); });
}
let searchGeneration = 0;
async function loadItems() {
    const generation = ++searchGeneration;
    const response = await api({ action: "items", query: $("search").value, category: $("category").value, page });
    if (generation !== searchGeneration) return;
    total = response.total;
    $("item-count").textContent = `${total.toLocaleString()} items`;
    $("page-count").textContent = `${page + 1} / ${Math.max(1, Math.ceil(total / 48))}`;
    $("prev").disabled = page === 0;
    $("next").disabled = (page + 1) * 48 >= total;
    $("item-list").replaceChildren(...response.items.map((item) => {
        const row = document.createElement("button"); row.className = "item-row"; row.dataset.id = item.id; row.setAttribute("role", "option"); row.setAttribute("aria-selected", String(selected?.id === item.id));
        const name = document.createElement("strong"); name.textContent = item.name;
        const sub = document.createElement("small"); sub.textContent = `${item.category} · Quality 1${item.maximumQuality > 1 ? `–${item.maximumQuality}` : ""}`;
        row.append(name, sub); row.onclick = () => choose(item); row.classList.toggle("selected", selected?.id === item.id); return row;
    }));
    if (!response.items.length) { const empty = document.createElement("p"); empty.className = "empty"; empty.textContent = "No equipment matches your search."; $("item-list").append(empty); }
}
function drawLocations() {
    const query = $("location-search").value.toLowerCase();
    $("location-list").replaceChildren(...locations.filter((row) => `${row.name} ${row.group}`.toLowerCase().includes(query)).map((location) => {
        const button = document.createElement("button"); button.className = "location";
        const content = document.createElement("span"); const title = document.createElement("strong"); title.textContent = location.name;
        const group = document.createElement("small"); group.textContent = location.group; content.append(title, group);
        const arrow = document.createElement("span"); arrow.textContent = "↗"; button.append(content, arrow);
        button.onclick = action(() => api({ action: "teleport", location: location.id })); return button;
    }));
}
async function loadRecipes() {
    const response = await api({ action: "recipes", kind: $("craft-kind").value }); recipeRows = response.recipes;
    $("recipe").replaceChildren(option("all", `All ${recipeRows.length} enabled recipes`), ...recipeRows.map((row) => option(row.id, row.name)));
    ingredients();
}
function ingredients() {
    const recipe = recipeRows.find((row) => row.id === $("recipe").value);
    $("ingredient-summary").textContent = recipe ? recipe.ingredients.map((row) => `${row.amount} × ${row.name}`).join(", ") : "Ingredients for one craft of each selected recipe.";
}
async function showTab(next) {
    section = ["items", "locations", "crafting", "npcs"].includes(next) ? next : "items";
    document.querySelectorAll(".page").forEach((node) => node.hidden = node.id !== section);
    document.querySelectorAll("[data-tab]").forEach((node) => node.classList.toggle("active", node.dataset.tab === section));
    if (section === "items") await loadItems();
    if (section === "locations") drawLocations();
    if (section === "crafting") await loadRecipes();
}
window.addEventListener("playground:open", (event) => action(async () => {
    $("cursor-note").textContent = event.detail.cursor ? "Cursor position captured. Drops appear at the ground you aimed at." : "No ground captured. Close the panel and aim at nearby ground first.";
    const response = await api({ action: "init" });
    $("level").textContent = ({ trosecko: "Trosky", kutnohorsko: "Kuttenberg", klaster: "Sedletz Monastery" })[response.level] || response.level;
    locations = response.locations;
    $("role").replaceChildren(...response.roles.map((role) => option(role, role.replaceAll("_", " "))));
    const category = $("category").value;
    $("category").replaceChildren(option("", "All equipment"), ...response.categories.map((row) => option(row, row))); $("category").value = category;
    await showTab(section);
})());
document.querySelectorAll("[data-tab]").forEach((button) => button.onclick = action(() => showTab(button.dataset.tab)));
$("close").onclick = () => send("playground:close");
document.addEventListener("keydown", (event) => { if (event.key === "Escape") send("playground:close"); });
$("search").oninput = () => { clearTimeout(searchTimer); searchTimer = setTimeout(action(() => { page = 0; return loadItems(); }), 250); };
$("category").onchange = action(() => { page = 0; return loadItems(); });
$("prev").onclick = action(() => { page = Math.max(0, page - 1); return loadItems(); });
$("next").onclick = action(() => { page++; return loadItems(); });
$("spawn-item").onclick = action(() => { if (selected) return api({ action: "item", item: selected.id, quality: Number($("quality").value), amount: Number($("amount").value), destination: document.querySelector('[name="destination"]:checked').value }); });
$("location-search").oninput = drawLocations;
$("dice").onclick = action(() => api({ action: "dice" }));
$("craft-kind").onchange = action(loadRecipes);
$("recipe").onchange = ingredients;
for (const name of ["learn", "materials"]) $(name).onclick = action(() => api({ action: name, kind: $("craft-kind").value, recipe: $("recipe").value, rounds: Number($("rounds").value) }));
$("spawn-npc").onclick = action(() => api({ action: "spawn", kind: "npc", role: $("role").value, follow: $("follow").checked }));
$("spawn-horse").onclick = action(() => api({ action: "spawn", kind: "horse" }));
$("spawn-dog").onclick = action(() => api({ action: "spawn", kind: "dog" }));
$("clear").onclick = action(() => api({ action: "clear" }));
$("creator").onclick = action(() => api({ action: "creator" }));
send("playground:ready");
