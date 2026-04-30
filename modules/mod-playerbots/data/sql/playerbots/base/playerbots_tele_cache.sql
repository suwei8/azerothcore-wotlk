DROP TABLE IF EXISTS `playerbots_tele_cache`;
CREATE TABLE `playerbots_tele_cache` (
  `id` INT(11) auto_increment,
  `level` TINYINT(3) NOT NULL,
  `map_id` mediumint(8) NOT NULL,
  `x` float(8) NOT NULL,
  `y` float(8) NOT NULL,
  `z` float(8) NOT NULL,
PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 ROW_FORMAT=DYNAMIC COMMENT='Playerbots Tele Cache';
