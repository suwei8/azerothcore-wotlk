DROP TABLE IF EXISTS `playerbots_rnditem_cache`;
CREATE TABLE `playerbots_rnditem_cache` (
  `id` INT(11) auto_increment,
  `lvl` INT(11) NOT NULL,
  `type` INT(11) NOT NULL,
  `item` INT(11) NOT NULL,
PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 ROW_FORMAT=DYNAMIC COMMENT='Playerbots Random Item Cache';
