const base = process.env.VUEPRESS_BASE || "/";

module.exports = {
  base,
  title: "Chirp Docs",
  description: "Documentation for Project Chirp",
  themeConfig: {
    nav: [{ text: "Home", link: "/" }],
    sidebar: [
      "/game_chat_features.md",
      "/game_chat_architecture.md",
      "/game_combat_best_practices.md"
    ]
  }
};
